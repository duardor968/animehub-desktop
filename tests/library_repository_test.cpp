#include "api/api_json.h"
#include "http_test_server.h"
#include "library/library_repository.h"
#include "library/library_service.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using namespace animehub::library;

namespace {

// The first schema as released, frozen here to check the migrations from it.
constexpr auto kVersionOne = R"sql(
CREATE TABLE anime (
    slug              TEXT PRIMARY KEY,
    title             TEXT NOT NULL,
    alternative_title TEXT NOT NULL DEFAULT '',
    synopsis          TEXT NOT NULL DEFAULT '',
    poster_url        TEXT NOT NULL DEFAULT '',
    backdrop_url      TEXT NOT NULL DEFAULT '',
    category          TEXT NOT NULL DEFAULT '',
    status            TEXT NOT NULL DEFAULT 'UNKNOWN',
    episode_count     INTEGER,
    genres            TEXT NOT NULL DEFAULT '',
    start_year        INTEGER,
    in_library        INTEGER NOT NULL DEFAULT 0,
    added_at          INTEGER,
    favorite          INTEGER NOT NULL DEFAULT 0,
    pending           INTEGER NOT NULL DEFAULT 0,
    notify            INTEGER NOT NULL DEFAULT 1,
    auto_download     INTEGER NOT NULL DEFAULT 0,
    last_activity_at  INTEGER,
    refreshed_at      INTEGER,
    episodes_synced_at INTEGER
) STRICT;

CREATE TABLE episode (
    slug         TEXT NOT NULL REFERENCES anime(slug) ON DELETE CASCADE,
    number       REAL NOT NULL,
    title        TEXT NOT NULL DEFAULT '',
    image_url    TEXT NOT NULL DEFAULT '',
    published_at INTEGER,
    known_at     INTEGER NOT NULL,
    is_new       INTEGER NOT NULL DEFAULT 0,
    state        INTEGER NOT NULL DEFAULT 0,
    position_ms  INTEGER NOT NULL DEFAULT 0,
    duration_ms  INTEGER NOT NULL DEFAULT 0,
    state_at     INTEGER,
    file_path    TEXT,
    file_size    INTEGER,
    PRIMARY KEY (slug, number)
) STRICT;

CREATE TABLE download (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    slug         TEXT NOT NULL,
    number       REAL NOT NULL,
    anime_title  TEXT NOT NULL,
    audio        TEXT NOT NULL DEFAULT 'SUB',
    origin       TEXT NOT NULL DEFAULT 'manual',
    state        TEXT NOT NULL,
    provider     TEXT,
    source_id    TEXT,
    target_path  TEXT NOT NULL,
    bytes_done   INTEGER NOT NULL DEFAULT 0,
    bytes_total  INTEGER NOT NULL DEFAULT 0,
    error_code   TEXT,
    error_detail TEXT,
    attempts     INTEGER NOT NULL DEFAULT 0,
    retry_at     INTEGER,
    queue_order  INTEGER NOT NULL,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    finished_at  INTEGER
) STRICT;

CREATE UNIQUE INDEX download_active_episode ON download(slug, number, audio)
    WHERE state NOT IN ('completed', 'cancelled');
CREATE UNIQUE INDEX download_active_target ON download(target_path COLLATE NOCASE)
    WHERE state NOT IN ('completed', 'cancelled');
CREATE INDEX episode_activity ON episode(slug, state, state_at);

INSERT INTO anime (slug, title, in_library, added_at, favorite, pending, notify)
    VALUES ('sousou-no-frieren', 'Sousou no Frieren', 1, 1000, 1, 1, 0);
INSERT INTO episode (slug, number, known_at, state, file_path, file_size)
    VALUES ('sousou-no-frieren', 1, 1000, 2, 'D:/Anime/Frieren - 01.mp4', 5000);
INSERT INTO download (slug, number, anime_title, state, target_path, queue_order, created_at,
                      updated_at)
    VALUES ('sousou-no-frieren', 2, 'Sousou no Frieren', 'paused', 'D:/Anime/Frieren - 02', 1,
            1000, 1000);
PRAGMA user_version = 1;
)sql";

AnimeRecord frieren() {
    AnimeRecord record;
    record.slug = QStringLiteral("sousou-no-frieren");
    record.title = QStringLiteral("Sousou no Frieren");
    record.alternativeTitle = QStringLiteral("Frieren: Beyond Journey's End");
    record.status = api::AnimeStatus::Finished;
    record.episodeCount = 4;
    record.genres = QStringLiteral("Aventura, Drama");
    return record;
}

template <class T> const char* why(const storage::Stored<T>& stored) {
    static QByteArray text;
    text = stored ? QByteArray() : stored.error().message.toUtf8();
    return text.constData();
}

QList<api::Episode> episodes(int count) {
    QList<api::Episode> list;
    for (int number = 1; number <= count; ++number) {
        list.append({.id = QString::number(number), .number = static_cast<double>(number)});
    }
    return list;
}

QByteArray fixture(const char* name) {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/") + QLatin1StringView(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", name);
    }
    return file.readAll();
}

constexpr auto kOneEpisode =
    R"({"data":[{"id":"1","number":1,"title":null,"imageUrl":null,"publishedAt":null}],)"
    R"("meta":{"page":1,"perPage":50,"totalPages":1,"totalRecords":1,"firstNumber":1,)"
    R"("lastNumber":1}})";

api::Relation relationTo(const QString& slug, api::RelationKind kind = api::RelationKind::Sequel) {
    api::Relation relation;
    relation.kind = kind;
    relation.anime.slug = slug;
    relation.anime.title = slug;
    return relation;
}

// What the library's refresh of `slug` receives.
api::AnimeDetail detailOf(const QString& slug, const QList<api::Relation>& relations) {
    api::AnimeDetail detail;
    detail.slug = slug;
    detail.title = slug;
    detail.relations = relations;
    return detail;
}

} // namespace

class LibraryRepositoryTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<LibraryRepository> repository_;

    [[nodiscard]] int storedRelations() const {
        auto query = database_->run(QStringLiteral("SELECT COUNT(*) FROM anime_relation"));
        return query && query->next() ? query->value(0).toInt() : -1;
    }

    [[nodiscard]] bool inLibrary(const QString& slug) const {
        const auto record = repository_->anime(slug);
        return record && *record && (*record)->inLibrary;
    }

private slots:
    void init() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY2(opened, qPrintable(opened ? QString() : opened.error().message));
        database_ = std::move(*opened);
        repository_ = std::make_unique<LibraryRepository>(*database_);
        const auto saved = repository_->saveSnapshot(frieren());
        QVERIFY2(saved, why(saved));
    }

    void cleanup() {
        repository_.reset();
        database_.reset();
    }

    void migratesToTheCurrentSchema() {
        QCOMPARE(database_->schemaVersion(), 7);
    }

    void fileReferenceLookupUsesTheReverseIndex() {
        const auto query =
            QStringLiteral("EXPLAIN QUERY PLAN DELETE FROM media_file WHERE path_key = ? AND NOT "
                           "EXISTS (SELECT 1 FROM episode_file WHERE file_id = media_file.id)");
        auto plan = database_->run(query, {QStringLiteral("c:/media/test.mkv")});
        QVERIFY(plan);
        QStringList steps;
        while (plan->next())
            steps.append(plan->value(3).toString());
        const auto text = steps.join(u'\n');
        QVERIFY2(text.contains(QStringLiteral(
                     "SEARCH episode_file USING COVERING INDEX episode_file_file_id")),
                 qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("SCAN episode_file")), qPrintable(text));
    }

    void migratesAVersionOneLibrary_data() {
        QTest::addColumn<int>("version");
        QTest::newRow("version-one") << 1;
        QTest::newRow("version-three") << 3;
    }

    void migratesAVersionOneLibrary() {
        QFETCH(int, version);
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("library.sqlite3"));
        {
            auto raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            raw.setDatabaseName(path);
            QVERIFY(raw.open());
            {
                QSqlQuery query(raw);
                for (const auto& statement :
                     QString::fromUtf8(kVersionOne).split(u';', Qt::SkipEmptyParts)) {
                    if (!statement.trimmed().isEmpty()) {
                        QVERIFY2(query.exec(statement), qPrintable(query.lastError().text()));
                    }
                }
                if (version == 3) {
                    QVERIFY(query.exec(QStringLiteral("ALTER TABLE anime DROP COLUMN pending")));
                    QVERIFY(
                        query.exec(QStringLiteral("ALTER TABLE download ADD COLUMN paused_with_all "
                                                  "INTEGER NOT NULL DEFAULT 0")));
                    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 3")));
                }
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));

        auto opened = storage::Database::open(path);
        QVERIFY2(opened, qPrintable(opened ? QString() : opened.error().message));
        const auto& database = **opened;
        QCOMPARE(database.schemaVersion(), 7);
        // The library as it was before the migration stays beside it.
        const auto copy = QStringLiteral("%1.schema-%2.bak").arg(path).arg(version);
        QVERIFY(QFileInfo::exists(copy));
        {
            auto saved =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("copy"));
            saved.setDatabaseName(copy);
            QVERIFY(saved.open());
            QSqlQuery query(saved);
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")) && query.next());
            QCOMPARE(query.value(0).toInt(), version);
            saved.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("copy"));
        auto columns = database.run(QStringLiteral("SELECT name FROM pragma_table_info('anime')"));
        QVERIFY(columns);
        QStringList names;
        while (columns->next()) {
            names.append(columns->value(0).toString());
        }
        QVERIFY(!names.contains(QStringLiteral("pending")));
        QVERIFY(names.contains(QStringLiteral("favorite")));

        // Everything else stays: flags, progress, files and the download queue, none of it
        // paused all together.
        const LibraryRepository repository(**opened);
        const auto entry = repository.entry(QStringLiteral("sousou-no-frieren"));
        QVERIFY(entry && *entry);
        QVERIFY((*entry)->inLibrary);
        QVERIFY((*entry)->favorite);
        QVERIFY(!(*entry)->notify);
        QCOMPARE((*entry)->watchedEpisodes, 1);
        QCOMPARE((*entry)->downloadedBytes, 5000);
        QCOMPARE(repository.episodeLocations(QStringLiteral("sousou-no-frieren"), 1)->size(), 1);
        auto queue =
            database.run(QStringLiteral("SELECT COUNT(*) FROM download WHERE paused_with_all = 0"));
        QVERIFY(queue && queue->next());
        QCOMPARE(queue->value(0).toInt(), 1);
    }

    void refusesALibraryFromANewerVersion() {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("library.sqlite3"));
        {
            auto opened = storage::Database::open(path);
            QVERIFY(opened);
            QVERIFY((*opened)->run(QStringLiteral("PRAGMA user_version = 99")));
        }
        auto reopened = storage::Database::open(path);
        QVERIFY(!reopened);
        QVERIFY(reopened.error().message.contains(u"newer"));
    }

    void keepsUserFlagsAndRicherMetadataOnRefresh() {
        QVERIFY(repository_->setInLibrary(QStringLiteral("sousou-no-frieren"), true));
        QVERIFY(repository_->setFlag(QStringLiteral("sousou-no-frieren"), Flag::Favorite, true));

        AnimeRecord summary; // what a catalog card carries: no genres, no alternative title
        summary.slug = QStringLiteral("sousou-no-frieren");
        summary.title = QStringLiteral("Sousou no Frieren (renamed)");
        summary.status = api::AnimeStatus::Finished;
        QVERIFY(repository_->saveSnapshot(summary));

        const auto stored = repository_->anime(QStringLiteral("sousou-no-frieren"));
        QVERIFY(stored && *stored);
        QCOMPARE((*stored)->title, QStringLiteral("Sousou no Frieren (renamed)"));
        QCOMPARE((*stored)->genres, QStringLiteral("Aventura, Drama"));
        QCOMPARE((*stored)->alternativeTitle, QStringLiteral("Frieren: Beyond Journey's End"));
        QCOMPARE((*stored)->episodeCount, 4);
        QVERIFY((*stored)->inLibrary);
        QVERIFY((*stored)->favorite);
        QVERIFY((*stored)->addedAt.isValid());
    }

    void removingFromTheLibraryDisablesAutomaticDownloads() {
        const auto slug = QStringLiteral("sousou-no-frieren");
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->setFlag(slug, Flag::AutoDownload, true));
        QVERIFY(repository_->setInLibrary(slug, false));
        const auto stored = repository_->anime(slug);
        QVERIFY(!(*stored)->inLibrary);
        QVERIFY(!(*stored)->autoDownload);
        QVERIFY(!repository_->setInLibrary(QStringLiteral("unknown"), true));
    }

    void reportsAndFlagsNewEpisodes() {
        const auto slug = QStringLiteral("sousou-no-frieren");
        auto first = repository_->syncEpisodes(slug, episodes(2), false);
        QVERIFY(first);
        QCOMPARE(first->size(), 2);

        auto second = repository_->syncEpisodes(slug, episodes(4), true);
        QVERIFY(second);
        QCOMPARE(*second, (QList<double>{3, 4}));

        auto stored = repository_->episodes(slug);
        QCOMPARE(stored->size(), 4);
        QVERIFY(!stored->at(0).isNew);
        QVERIFY(stored->at(3).isNew);

        QVERIFY(repository_->clearNew(slug));
        stored = repository_->episodes(slug);
        QVERIFY(std::ranges::none_of(*stored, &EpisodeRecord::isNew));
    }

    void resumesTheEpisodeInProgressElseTheNextUnwatched() {
        const auto slug = QStringLiteral("sousou-no-frieren");
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->syncEpisodes(slug, episodes(4), false));

        auto entry = repository_->entry(slug);
        QCOMPARE((*entry)->resumeNumber, 1.0);

        QVERIFY(repository_->setWatchedThrough(slug, 2));
        entry = repository_->entry(slug);
        QCOMPARE((*entry)->watchedEpisodes, 2);
        QCOMPARE((*entry)->resumeNumber, 3.0);

        QVERIFY(repository_->setWatchState(slug, 4, WatchState::Watching, 61'000, 1'440'000));
        QVERIFY(repository_->setWatchState(slug, 3, WatchState::Watching, 5'000, 1'440'000));
        entry = repository_->entry(slug);
        QCOMPARE((*entry)->resumeNumber, 3.0);
        QCOMPARE((*entry)->resumePositionMs, 5'000);
        const auto fourth = repository_->episode(slug, 4);
        QCOMPARE((*fourth)->state, WatchState::Unwatched); // one episode in progress at a time
        QVERIFY((*entry)->lastActivityAt.isValid());

        QVERIFY(repository_->setWatchedThrough(slug, 4));
        entry = repository_->entry(slug);
        QVERIFY(!(*entry)->resumeNumber);
        QVERIFY((*entry)->finished());

        QVERIFY(repository_->setAllUnwatched(slug));
        entry = repository_->entry(slug);
        QCOMPARE((*entry)->watchedEpisodes, 0);
        QVERIFY(!(*entry)->finished());
    }

    void keepsFractionalEpisodes() {
        const auto slug = QStringLiteral("sousou-no-frieren");
        QVERIFY(repository_->syncEpisodes(slug,
                                          {{.id = QStringLiteral("a"), .number = 12},
                                           {.id = QStringLiteral("b"), .number = 12.5}},
                                          false));
        QVERIFY(repository_->setWatchState(slug, 12.5, WatchState::Watched));
        QCOMPARE((*repository_->episode(slug, 12.5))->state, WatchState::Watched);
        QCOMPARE((*repository_->episode(slug, 12))->state, WatchState::Unwatched);
        QCOMPARE(formatEpisodeNumber(12.5), QStringLiteral("12.5"));
        QCOMPARE(formatEpisodeNumber(1180), QStringLiteral("1180"));
    }

    void tracksDownloadedFiles() {
        const auto slug = QStringLiteral("sousou-no-frieren");
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->setEpisodeFile(slug, 2, QStringLiteral("D:/Anime/Frieren - 02.mp4"),
                                            1234));
        QVERIFY(repository_->setEpisodeFile(slug, 3, QStringLiteral("D:/Anime/Frieren - 03.mp4"),
                                            1000));
        QCOMPARE((*repository_->entry(slug))->downloadedEpisodes, 2);
        QCOMPARE((*repository_->entry(slug))->downloadedBytes, 2234);
        const auto files = repository_->episodeFiles();
        QCOMPARE(files->size(), 2);
        QVERIFY(repository_->clearEpisodeFile(slug, 2));
        QCOMPARE((*repository_->entry(slug))->downloadedEpisodes, 1);
        QCOMPARE((*repository_->entry(slug))->downloadedBytes, 1000);
    }

    void movesFilesToTheRecycleBin() {
        const auto slug = QStringLiteral("sousou-no-frieren");
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->syncEpisodes(slug, episodes(4), false));
        QTemporaryDir dir;
        QStringList paths;
        for (int number = 1; number <= 4; ++number) {
            paths.append(dir.filePath(QStringLiteral("Frieren - %1.mp4").arg(number)));
            QFile file(paths.last());
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(number * 100, 'x'));
            file.close();
            QVERIFY(repository_->setEpisodeFile(slug, number, paths.last(), number * 100));
        }
        QVERIFY(repository_->setWatchedThrough(slug, 2));

        // The real Recycle Bin stays out of the tests: a folder stands in for it, and episode 3
        // is in use.
        QNetworkAccessManager network;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:9/"))});
        QTemporaryDir bin;
        QStringList moved;
        LibraryService service(
            *repository_, api, nullptr,
            [&](const QString& path) -> std::optional<QString> {
                if (path == paths[2]) {
                    return std::nullopt;
                }
                moved.append(path);
                const auto trash_path = bin.filePath(QFileInfo(path).fileName());
                return QFile::rename(path, trash_path) ? std::optional(trash_path) : std::nullopt;
            },
            [](const QString& trash_path, const QString& path) {
                return QFile::rename(trash_path, path);
            });
        QSignalSpy changed(&service, &LibraryService::animeChanged);

        const auto watched = service.trashWatched(slug);
        QVERIFY2(watched, why(watched));
        QCOMPARE(watched->count, 2);
        QCOMPARE(watched->bytes, 300);
        QCOMPARE(watched->failed, 0);
        QCOMPARE(moved, (QStringList{paths[0], paths[1]}));
        QVERIFY((*repository_->episode(slug, 1))->filePath.isEmpty());
        QVERIFY((*repository_->episode(slug, 2))->filePath.isEmpty());
        QCOMPARE((*repository_->entry(slug))->downloadedBytes, 700);
        QCOMPARE(changed.size(), 1);

        // Undone: the files and their episodes are back; one already gone from the bin stays out.
        const auto restored = service.restore(slug, watched->files);
        QVERIFY2(restored, why(restored));
        QCOMPARE(*restored, 2);
        QVERIFY(QFile::exists(paths[0]));
        QCOMPARE((*repository_->episode(slug, 1))->filePath, paths[0]);
        QCOMPARE((*repository_->entry(slug))->downloadedBytes, 1000);
        QCOMPARE(changed.size(), 2);
        const auto again_trashed = service.trashFiles(slug, {1});
        QVERIFY(QFile::remove(again_trashed->files.first().trashPath));
        QCOMPARE(*service.restore(slug, again_trashed->files), 0);
        QVERIFY((*repository_->episode(slug, 1))->filePath.isEmpty());
        moved.clear();
        QCOMPARE(service.trashWatched(slug)->count, 1);
        QCOMPARE(moved, QStringList{paths[1]});
        changed.clear();

        // Episode 4 was deleted outside AnimeHub: it only loses its path.
        QVERIFY(QFile::remove(paths[3]));
        const auto chosen = service.trashFiles(slug, {3, 4, 9});
        QVERIFY2(chosen, why(chosen));
        QCOMPARE(chosen->count, 0);
        QCOMPARE(chosen->bytes, 0);
        QCOMPARE(chosen->failed, 1);
        QCOMPARE((*repository_->episode(slug, 3))->filePath, paths[2]);
        QVERIFY((*repository_->episode(slug, 4))->filePath.isEmpty());
        QCOMPARE(moved.size(), 1);

        // Nothing left to move: nothing changes.
        const auto again = service.trashWatched(slug);
        QCOMPARE(again->count, 0);
        QCOMPARE(changed.size(), 1);
    }

    void archivesKeepFollowingAndRemovalLeavesPhysicalFiles() {
        const auto slug = frieren().slug;
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("episode.mkv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("media");
        file.close();
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->setEpisodeFile(slug, 1, path, 5));
        QVERIFY(repository_->setWatchState(slug, 1, WatchState::Watched));
        QVERIFY(repository_->setArchived(slug, true));
        QCOMPARE(repository_->entries()->size(), 0);
        QCOMPARE(repository_->entries(true)->size(), 1);
        QCOMPARE(repository_->followedEntries()->size(), 1);
        QVERIFY((*repository_->anime(slug))->notify);
        QVERIFY(repository_->removePersonalRecord(slug));
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(repository_->followedEntries()->size(), 0);
        const auto record = repository_->episode(slug, 1);
        QCOMPARE((*record)->state, WatchState::Unwatched);
        QVERIFY(!(*record)->stateAt.isValid());
        QVERIFY((*record)->filePath.isEmpty());
        QVERIFY(repository_->episodeFiles()->isEmpty());
        auto files = database_->run(QStringLiteral("SELECT COUNT(*) FROM media_file"));
        QVERIFY(files && files->next());
        QCOMPARE(files->value(0).toInt(), 0);
    }

    void removedRecordStartsOverWhenAddedAgain() {
        const auto slug = frieren().slug;
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->setFlag(slug, Flag::Favorite, true));
        QVERIFY(repository_->setFlag(slug, Flag::Notify, false));
        QVERIFY(repository_->setFlag(slug, Flag::AutoDownload, true));
        QVERIFY(repository_->setWatchState(slug, 1, WatchState::Watched));
        QVERIFY(repository_->setArchived(slug, true));
        QVERIFY(repository_->removePersonalRecord(slug));

        // Added again, it is a new entry: notices on by default, nothing else kept.
        QVERIFY(repository_->setInLibrary(slug, true));
        const auto entry = repository_->entry(slug);
        QVERIFY(entry && *entry);
        QVERIFY((*entry)->inLibrary);
        QVERIFY((*entry)->notify);
        QVERIFY(!(*entry)->favorite);
        QVERIFY(!(*entry)->autoDownload);
        QVERIFY(!(*entry)->archived);
        QCOMPARE((*entry)->watchedEpisodes, 0);
        QCOMPARE(repository_->followedEntries()->size(), 1);
    }

    void importsMultipleCopiesWithoutInventingWatchDates() {
        const auto slug = frieren().slug;
        QTemporaryDir dir;
        const auto first = dir.filePath(QStringLiteral("episode.mkv"));
        const auto second = dir.filePath(QStringLiteral("copy.mkv"));
        QVERIFY(repository_->importEpisodeFile(slug, 1, first, 100, true));
        QVERIFY(repository_->importEpisodeFile(slug, 1, first.toUpper(), 100, false));
        QVERIFY(repository_->importEpisodeFile(slug, 1, second, 100, false));
        QCOMPARE(repository_->episodeLocations(slug, 1)->size(), 2);
        auto episode = repository_->episode(slug, 1);
        QCOMPARE((*episode)->state, WatchState::Watched);
        QVERIFY(!(*episode)->stateAt.isValid());
        QVERIFY(repository_->removeEpisodeFile(slug, 1, first));
        QCOMPARE(repository_->episodeLocations(slug, 1)->size(), 1);
        QCOMPARE((*repository_->episode(slug, 1))->state, WatchState::Watched);
        QCOMPARE((*repository_->episode(slug, 1))->filePath, second);
        QVERIFY(repository_->clearEpisodeFile(slug, 1));
        QCOMPARE((*repository_->episode(slug, 1))->state, WatchState::Watched);
        auto remaining_files = database_->run(QStringLiteral("SELECT COUNT(*) FROM media_file"));
        QVERIFY(remaining_files && remaining_files->next());
        QCOMPARE(remaining_files->value(0).toInt(), 0);
        remaining_files->finish();
        QVERIFY(repository_->setWatchState(slug, 2, WatchState::Watching, 4000, 8000));
        QVERIFY(repository_->importEpisodeFile(slug, 2, first, 100, false));
        QCOMPARE((*repository_->episode(slug, 2))->positionMs, 4000);
    }

    void explicitEditionCanMapOneFileToSeveralEpisodes() {
        const auto slug = frieren().slug;
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("double.mkv"));
        QVERIFY(repository_->setEpisodeFile(slug, 1, path, 500));
        QVERIFY(repository_->setEpisodeFile(slug, 2, path, 500));
        QVERIFY(repository_->removeEpisodeFile(slug, 1, path));
        QCOMPARE(repository_->episodeLocations(slug, 2)->size(), 1);
        QCOMPARE(repository_->episodeFiles()->size(), 1);
        QVERIFY(repository_->removeEpisodeFile(slug, 2, path));
        auto remaining_files = database_->run(QStringLiteral("SELECT COUNT(*) FROM media_file"));
        QVERIFY(remaining_files && remaining_files->next());
        QCOMPARE(remaining_files->value(0).toInt(), 0);
    }

    void selectsExistingCopyAndRestoresSharedEditionMappings() {
        const auto slug = frieren().slug;
        QTemporaryDir dir;
        const auto missing = dir.filePath(QStringLiteral("missing.mkv"));
        const auto path = dir.filePath(QStringLiteral("double.mkv"));
        const auto bin = dir.filePath(QStringLiteral("trashed.mkv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("media");
        file.close();
        QVERIFY(repository_->setEpisodeFile(slug, 1, missing, 5));
        QVERIFY(repository_->setEpisodeFile(slug, 1, path, 5));
        QVERIFY(repository_->setEpisodeFile(slug, 2, path, 5));
        QCOMPARE((*repository_->episode(slug, 1))->filePath, path);
        QCOMPARE((*repository_->entry(slug))->downloadedBytes, 10);
        QNetworkAccessManager network;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:9/"))});
        LibraryService service(
            *repository_, api, nullptr,
            [&](const QString& original) -> std::optional<QString> {
                return QFile::rename(original, bin) ? std::optional(bin) : std::nullopt;
            },
            [](const QString& original, const QString& destination) {
                return QFile::rename(original, destination);
            });
        QVERIFY(repository_->setWatchState(slug, 1, WatchState::Watched));
        const auto watched_only = service.trashWatched(slug);
        QVERIFY(watched_only);
        QCOMPARE(watched_only->count, 0);
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(repository_->episodeFileMappings(path)->size(), 2);
        const auto partial = service.trashFiles(slug, {1});
        QVERIFY(!partial);
        QVERIFY(partial.error().message.contains(QStringLiteral("fuera de la selección")));
        service.verifyFiles();
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(repository_->episodeFileMappings(path)->size(), 2);
        const auto trashed = service.trashFiles(slug, {1, 2});
        QVERIFY(trashed);
        QCOMPARE(trashed->count, 1);
        QCOMPARE(trashed->files.size(), 2);
        service.verifyFiles();
        QCOMPARE(*service.restore(slug, trashed->files), 1);
        QCOMPARE(repository_->episodeLocations(slug, 1)->size(), 1);
        QCOMPARE(repository_->episodeLocations(slug, 2)->size(), 1);
        QCOMPARE((*repository_->entry(slug))->downloadedBytes, 5);
        QCOMPARE((*repository_->episode(slug, 1))->state, WatchState::Watched);
        QCOMPARE((*repository_->episode(slug, 2))->state, WatchState::Unwatched);

        QVERIFY(repository_->setWatchState(slug, 2, WatchState::Watched));
        const auto all_watched = service.trashWatched(slug);
        QVERIFY(all_watched);
        QCOMPARE(all_watched->count, 1);
        QCOMPARE(all_watched->files.size(), 2);
        service.verifyFiles();
        QCOMPARE(*service.restore(slug, all_watched->files), 1);
        QCOMPARE(repository_->episodeFileMappings(path)->size(), 2);

        auto other = frieren();
        other.slug = QStringLiteral("another-edition");
        QVERIFY(repository_->saveSnapshot(other));
        QVERIFY(repository_->setEpisodeFile(other.slug, 1, path, 5));
        const auto cross_work = service.trashFiles(slug, {1, 2});
        QVERIFY(!cross_work);
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(repository_->episodeFileMappings(path)->size(), 3);
    }

    void manualFranchiseMembershipSurvivesRefresh() {
        // Frieren is the sequel of a followed anime in First.
        const auto slug = frieren().slug;
        auto anchor = frieren();
        anchor.slug = QStringLiteral("anchor");
        QVERIFY(repository_->saveSnapshot(anchor));
        QVERIFY(repository_->setInLibrary(anchor.slug, true));
        const auto first = repository_->createFranchise(QStringLiteral("First"), anchor.slug);
        const auto second = repository_->createFranchise(QStringLiteral("Second"));
        QVERIFY(first && second);
        const auto detail = detailOf(anchor.slug, {relationTo(slug)});
        QCOMPARE(*repository_->discoverFranchiseRelations(detail), QStringList{slug});
        QVERIFY(repository_->setFranchiseMember(*first, slug, false));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(repository_->franchises()->first().members, QStringList{anchor.slug});
        QVERIFY(repository_->setFranchiseMember(*second, slug, true));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QVERIFY(repository_->saveSnapshot(frieren()));
        const auto groups = repository_->franchises();
        QCOMPARE(groups->at(0).id, *first);
        QCOMPARE(groups->at(0).members, QStringList{anchor.slug});
        QCOMPARE(groups->at(1).members, QStringList{slug});
        QVERIFY(repository_->setFranchiseMember(*first, slug, true));
        QCOMPARE(repository_->franchises()->at(0).members, (QStringList{anchor.slug, slug}));
        QVERIFY(repository_->franchises()->at(1).members.isEmpty());
    }
    void discoversIncomingAndOutgoingRelationsForArchivedFinishedGroups_data() {
        QTest::addColumn<bool>("incoming");
        QTest::newRow("old-outgoing-to-new") << false;
        QTest::newRow("old-incoming-from-new") << true;
    }

    void discoversIncomingAndOutgoingRelationsForArchivedFinishedGroups() {
        QFETCH(bool, incoming);
        const auto old = frieren().slug;
        QVERIFY(repository_->setInLibrary(old, true));
        QVERIFY(repository_->setArchived(old, true));
        const auto group = repository_->createFranchise(QStringLiteral("My group"));
        QVERIFY(group);
        QVERIFY(repository_->setFranchiseMember(*group, old, true));
        api::AnimeDetail detail;
        detail.slug = old;
        detail.title = frieren().title;
        detail.status = api::AnimeStatus::Finished;
        api::Relation relation;
        relation.kind = incoming ? api::RelationKind::Prequel : api::RelationKind::Sequel;
        relation.anime.slug = QStringLiteral("new-season");
        relation.anime.title = QStringLiteral("New season");
        relation.anime.status = api::AnimeStatus::Upcoming;
        if (incoming)
            detail.incomingRelations.append(relation);
        else
            detail.relations.append(relation);
        const auto discovered = repository_->discoverFranchiseRelations(detail);
        QVERIFY(discovered);
        QCOMPARE(*discovered, QStringList{relation.anime.slug});
        const auto added = repository_->anime(relation.anime.slug);
        QVERIFY(added && *added);
        QVERIFY((*added)->inLibrary);
        QVERIFY((*added)->archived);
        QVERIFY((*added)->notify);
        QVERIFY(!(*added)->autoDownload);
        QVERIFY(repository_->episodes(relation.anime.slug)->isEmpty());
        QCOMPARE(repository_->franchises()->first().title, QStringLiteral("My group"));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QVERIFY(repository_->removePersonalRecord(relation.anime.slug));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QVERIFY(!(*repository_->anime(relation.anime.slug))->inLibrary);
        QVERIFY(repository_->setInLibrary(relation.anime.slug, true));
        QVERIFY((*repository_->anime(relation.anime.slug))->notify);
        QCOMPARE(repository_->discoverFranchiseRelations(detail)->size(), 1);
    }

    void newSeasonJoinsWithoutUndoingManualExclusion() {
        const auto old = frieren().slug;
        const auto season = QStringLiteral("new-season");
        QVERIFY(repository_->setInLibrary(old, true));
        const auto group = repository_->createFranchise(QStringLiteral("Personal"), old);
        QVERIFY(group);
        const auto detail = detailOf(old, {relationTo(season)});
        QCOMPARE(*repository_->discoverFranchiseRelations(detail), QStringList{season});
        QVERIFY(inLibrary(season));
        QVERIFY(repository_->setFranchiseMember(*group, season, false));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(repository_->franchises()->first().members, QStringList{old});
    }

    void ambiguousOrMissingRelationsDoNotCreateMembership() {
        // A followed anime of its own, related to two personal franchises.
        auto other = frieren();
        other.slug = QStringLiteral("another-show");
        QVERIFY(repository_->saveSnapshot(other));
        auto crossover = frieren();
        crossover.slug = QStringLiteral("crossover");
        QVERIFY(repository_->saveSnapshot(crossover));
        const auto first = repository_->createFranchise(QStringLiteral("First"));
        const auto second = repository_->createFranchise(QStringLiteral("Second"));
        QVERIFY(first && second);
        QVERIFY(repository_->setInLibrary(frieren().slug, true));
        QVERIFY(repository_->setInLibrary(other.slug, true));
        QVERIFY(repository_->setInLibrary(crossover.slug, true));
        QVERIFY(repository_->setFranchiseMember(*first, frieren().slug, true));
        QVERIFY(repository_->setFranchiseMember(*second, other.slug, true));
        auto detail = detailOf(crossover.slug, {});
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        detail.relations = {relationTo(frieren().slug, api::RelationKind::Prequel),
                            relationTo(other.slug, api::RelationKind::SideStory)};
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(repository_->franchises()->size(), 2);
        QCOMPARE(repository_->franchises()->at(0).members, QStringList{frieren().slug});
        QCOMPARE(repository_->franchises()->at(1).members, QStringList{other.slug});
        // A non-followed neighboring group still prevents an automatic merge.
        QVERIFY(repository_->setFlag(other.slug, Flag::Notify, false));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(repository_->franchises()->size(), 2);
        QCOMPARE(repository_->franchises()->at(0).members, QStringList{frieren().slug});
        QCOMPARE(repository_->franchises()->at(1).members, QStringList{other.slug});
    }

    void unfollowedAnimeAddsNothing() {
        // Its prequel is in a followed franchise and its side story is followed on its own.
        const auto old = frieren().slug;
        QVERIFY(repository_->setInLibrary(old, true));
        QVERIFY(repository_->createFranchise(QStringLiteral("Personal"), old));
        auto side = frieren();
        side.slug = QStringLiteral("side-story");
        QVERIFY(repository_->saveSnapshot(side));
        QVERIFY(repository_->setInLibrary(side.slug, true));
        const auto detail = detailOf(QStringLiteral("catalog-only"),
                                     {relationTo(old, api::RelationKind::Prequel),
                                      relationTo(side.slug, api::RelationKind::SideStory)});
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QVERIFY(!inLibrary(detail.slug));
        QCOMPARE(storedRelations(), 0);
        // In the library without notices or downloads, it is not followed either.
        QVERIFY(repository_->setInLibrary(detail.slug, true));
        QVERIFY(repository_->setFlag(detail.slug, Flag::Notify, false));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(storedRelations(), 0);
        QCOMPARE(repository_->franchises()->first().members, QStringList{old});
    }

    void onlyInstallmentsJoin_data() {
        using api::RelationKind;
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("installment");
        QTest::newRow("prequel") << static_cast<int>(RelationKind::Prequel) << true;
        QTest::newRow("sequel") << static_cast<int>(RelationKind::Sequel) << true;
        QTest::newRow("side-story") << static_cast<int>(RelationKind::SideStory) << true;
        QTest::newRow("main-story") << static_cast<int>(RelationKind::MainStory) << true;
        QTest::newRow("full-story") << static_cast<int>(RelationKind::FullStory) << true;
        QTest::newRow("spin-off") << static_cast<int>(RelationKind::SpinOff) << false;
        QTest::newRow("summary") << static_cast<int>(RelationKind::Summary) << false;
        QTest::newRow("alternative") << static_cast<int>(RelationKind::Alternative) << false;
        QTest::newRow("alternative-setting")
            << static_cast<int>(RelationKind::AlternativeSetting) << false;
        QTest::newRow("other") << static_cast<int>(RelationKind::Other) << false;
    }

    void onlyInstallmentsJoin() {
        QFETCH(int, kind);
        QFETCH(bool, installment);
        const auto relation_kind = static_cast<api::RelationKind>(kind);
        // A followed franchise and a followed anime of its own, each related to one more.
        const auto old = frieren().slug;
        QVERIFY(repository_->setInLibrary(old, true));
        QVERIFY(repository_->createFranchise(QStringLiteral("Personal"), old));
        auto solo = frieren();
        solo.slug = QStringLiteral("solo");
        QVERIFY(repository_->saveSnapshot(solo));
        QVERIFY(repository_->setInLibrary(solo.slug, true));

        const auto joined = repository_->discoverFranchiseRelations(
            detailOf(old, {relationTo(QStringLiteral("related"), relation_kind)}));
        QVERIFY2(joined, why(joined));
        QCOMPARE(joined->size(), installment ? 1 : 0);
        QCOMPARE(repository_->entries(true)->size(), installment ? 3 : 2);
        QCOMPARE(repository_->franchises()->first().members.size(), installment ? 2 : 1);

        QVERIFY(repository_
                    ->discoverFranchiseRelations(detailOf(
                        solo.slug, {relationTo(QStringLiteral("solo-related"), relation_kind)}))
                    ->isEmpty());
        QVERIFY(!inLibrary(QStringLiteral("solo-related")));
        QCOMPARE(storedRelations(), installment ? 2 : 0);
    }

    void refreshDropsLinksThatNoLongerCount() {
        // Links stored when any relation counted.
        const auto old = frieren().slug;
        QVERIFY(repository_->setInLibrary(old, true));
        for (const auto& slug : {QStringLiteral("spin-off"), QStringLiteral("gone")}) {
            auto record = frieren();
            record.slug = slug;
            QVERIFY(repository_->saveSnapshot(record));
            QVERIFY(database_->run(QStringLiteral("INSERT INTO anime_relation(source_slug, "
                                                  "target_slug) VALUES (?, ?)"),
                                   {old, slug}));
        }
        QVERIFY(database_->run(
            QStringLiteral("INSERT INTO anime_relation(source_slug, target_slug) VALUES (?, ?)"),
            {QStringLiteral("spin-off"), old}));
        // The provider still relates the spin-off both ways and no longer relates the other one.
        auto detail =
            detailOf(old, {relationTo(QStringLiteral("spin-off"), api::RelationKind::SpinOff)});
        detail.incomingRelations.append(
            relationTo(QStringLiteral("spin-off"), api::RelationKind::SpinOff));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(storedRelations(), 0);
    }

    void removedWorkIsNotRediscoveredAndManualExclusionWins() {
        const auto old = frieren().slug;
        const auto season = QStringLiteral("removed-season");
        QVERIFY(repository_->setInLibrary(old, true));
        const auto group = repository_->createFranchise(QStringLiteral("Personal"), old);
        QVERIFY(group);
        const auto detail = detailOf(old, {relationTo(season)});
        QCOMPARE(*repository_->discoverFranchiseRelations(detail), QStringList{season});
        QVERIFY(repository_->removePersonalRecord(season));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QVERIFY(!inLibrary(season));
        QCOMPARE(repository_->franchises()->first().members, QStringList{old});
        // An explicit personal correction is allowed; its manual exclusion then wins.
        QVERIFY(repository_->setFranchiseMember(*group, season, true));
        QCOMPARE(repository_->franchises()->first().members, (QStringList{season, old}));
        QVERIFY(repository_->setFranchiseMember(*group, season, false));
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QCOMPARE(repository_->franchises()->first().members, QStringList{old});
    }

    void ambiguousRelationCreatesNothing() {
        // Two followed anime of their own share a sequel that is not in the library.
        const auto first = frieren().slug;
        auto other = frieren();
        other.slug = QStringLiteral("another-show");
        QVERIFY(repository_->saveSnapshot(other));
        QVERIFY(repository_->setInLibrary(first, true));
        QVERIFY(repository_->setInLibrary(other.slug, true));
        const auto sequel = QStringLiteral("shared-sequel");
        const auto memberships = [&] {
            auto query = database_->run(
                QStringLiteral("SELECT COUNT(*) FROM franchise_member WHERE slug = ?"), {sequel});
            return query && query->next() ? query->value(0).toInt() : -1;
        };
        // No franchise to join: none is created.
        for (const auto& slug : {first, other.slug})
            QVERIFY(repository_->discoverFranchiseRelations(detailOf(slug, {relationTo(sequel)}))
                        ->isEmpty());
        QCOMPARE(storedRelations(), 2);
        QVERIFY(!inLibrary(sequel));
        QVERIFY(repository_->franchises()->isEmpty());
        // A franchise each: it could join either, so it joins neither.
        const auto one = repository_->createFranchise(QStringLiteral("One"), first);
        const auto two = repository_->createFranchise(QStringLiteral("Two"), other.slug);
        QVERIFY(one && two);
        const auto detail = detailOf(first, {relationTo(sequel)});
        QVERIFY(repository_->discoverFranchiseRelations(detail)->isEmpty());
        QVERIFY(!inLibrary(sequel));
        QCOMPARE(memberships(), 0);
        QCOMPARE(repository_->franchises()->size(), 2);
        QCOMPARE(repository_->franchises()->at(0).members, QStringList{first});
        QCOMPARE(repository_->franchises()->at(1).members, QStringList{other.slug});
        // With one franchise left, the same refresh adds it there.
        QVERIFY(repository_->setFranchiseMember(*two, other.slug, false));
        QCOMPARE(*repository_->discoverFranchiseRelations(detail), QStringList{sequel});
        QCOMPARE(repository_->franchises()->at(0).members, (QStringList{sequel, first}));
    }

    void franchiseWithAnUnknownWorkIsNotCreated() {
        QVERIFY(
            !repository_->createFranchise(QStringLiteral("Bad member"), QStringLiteral("missing")));
        QVERIFY(repository_->franchises()->isEmpty());
    }

    void openingAnAnimePageAddsNothing() {
        testing::HttpTestServer server;
        QVERIFY(server.listen());
        server.json("/api/v1/anime/sousou-no-frieren/episodes", kOneEpisode);
        QNetworkAccessManager network;
        api::ApiClient api(network, {.baseUrl = server.url(QStringLiteral("/api/v1/"))});
        LibraryService service(*repository_, api);
        QSignalSpy members(&service, &LibraryService::newFranchiseMembers);
        // Not in the library, while its sequel is in a followed franchise.
        const auto detail = api::json::decodeAnime(fixture("anime.json"));
        QVERIFY(detail);
        const auto sequel = detail->relations.first().anime;
        QCOMPARE(detail->relations.first().kind, api::RelationKind::Sequel);
        QVERIFY(repository_->saveSnapshot(snapshotOf(sequel)));
        QVERIFY(repository_->setInLibrary(sequel.slug, true));
        QVERIFY(repository_->createFranchise(QStringLiteral("Frieren"), sequel.slug));

        service.refresh(*detail, false);
        QTRY_VERIFY(!service.isRefreshing(detail->slug));
        QCOMPARE(repository_->episodes(detail->slug)->size(), 1); // its page still gets episodes
        QVERIFY(!inLibrary(detail->slug));
        QCOMPARE(repository_->franchises()->first().members, QStringList{sequel.slug});
        QCOMPARE(storedRelations(), 0);
        QVERIFY(members.isEmpty());
    }

    void libraryRefreshAddsTheNewSeasonOfAFollowedFranchise() {
        testing::HttpTestServer server;
        QVERIFY(server.listen());
        server.json("/api/v1/anime/sousou-no-frieren", fixture("anime.json"));
        server.json("/api/v1/anime/sousou-no-frieren/episodes", kOneEpisode);
        QNetworkAccessManager network;
        api::ApiClient api(network, {.baseUrl = server.url(QStringLiteral("/api/v1/"))});
        LibraryService service(*repository_, api);
        QSignalSpy members(&service, &LibraryService::newFranchiseMembers);
        const auto slug = frieren().slug;
        QVERIFY(repository_->setInLibrary(slug, true));
        QVERIFY(repository_->createFranchise(QStringLiteral("Frieren"), slug));
        const auto detail = api::json::decodeAnime(fixture("anime.json"));
        QVERIFY(detail);
        const auto sequel = detail->relations.first().anime.slug;

        // Its page does not add the sequel either; the library's own refresh does.
        service.refresh(*detail, false);
        QTRY_VERIFY(!service.isRefreshing(slug));
        QVERIFY(!inLibrary(sequel));
        QVERIFY(members.isEmpty());

        service.refresh(slug, true);
        QTRY_COMPARE(members.size(), 1);
        QCOMPARE(members.first().first().toStringList(), QStringList{sequel});
        QTRY_VERIFY(!service.isRefreshing(slug));
        const auto added = repository_->anime(sequel);
        QVERIFY(added && *added);
        QVERIFY((*added)->inLibrary);
        QVERIFY((*added)->notify);
        QVERIFY(!(*added)->autoDownload);
        QCOMPARE(repository_->franchises()->first().members, (QStringList{slug, sequel}));
        QCOMPARE(server.count("/api/v1/anime/" + sequel.toUtf8() + "/episodes"), 0);
    }

    void listsOnlyTheLibrary() {
        auto other = frieren();
        other.slug = QStringLiteral("cowboy-bebop");
        other.title = QStringLiteral("Cowboy Bebop");
        QVERIFY(repository_->saveSnapshot(other));
        QVERIFY(repository_->setInLibrary(QStringLiteral("cowboy-bebop"), true));
        const auto entries = repository_->entries();
        QCOMPARE(entries->size(), 1);
        QCOMPARE(entries->first().slug, QStringLiteral("cowboy-bebop"));
    }
};

QTEST_GUILESS_MAIN(LibraryRepositoryTest)
#include "library_repository_test.moc"
