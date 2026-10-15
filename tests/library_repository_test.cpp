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

} // namespace

class LibraryRepositoryTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<LibraryRepository> repository_;

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
        QCOMPARE(database_->schemaVersion(), 3);
    }

    void migratesAVersionOneLibrary() {
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
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));

        auto opened = storage::Database::open(path);
        QVERIFY2(opened, qPrintable(opened ? QString() : opened.error().message));
        const auto& database = **opened;
        QCOMPARE(database.schemaVersion(), 3);
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
