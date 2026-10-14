#include "library/library_repository.h"

#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using namespace animehub::library;

namespace {

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
        QCOMPARE(database_->schemaVersion(), 1);
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
        QCOMPARE((*repository_->entry(slug))->downloadedEpisodes, 1);
        const auto files = repository_->episodeFiles();
        QCOMPARE(files->size(), 1);
        QVERIFY(repository_->clearEpisodeFile(slug, 2));
        QCOMPARE((*repository_->entry(slug))->downloadedEpisodes, 0);
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
