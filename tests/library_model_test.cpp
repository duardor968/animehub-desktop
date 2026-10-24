#include "app/library_model.h"

#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using app::LibraryModel;

class LibraryModelTest : public QObject {
    Q_OBJECT
private slots:
    void viewsArchiveAndExplicitMembership() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });
        for (const auto& slug : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = slug.toUpper();
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
        }
        auto group = repo.createFranchise(QStringLiteral("Series"));
        QVERIFY(group);
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("a"), true));
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("b"), true));
        LibraryModel model(service, downloads, api);
        QCOMPARE(model.rowCount(), 2); // a real group and an independent anime
        QCOMPARE(model.total(), 3);
        model.setGrouped(false);
        QCOMPARE(model.rowCount(), 3);
        QSet<QString> unique;
        for (int row = 0; row < model.rowCount(); ++row) {
            QVERIFY(model.groupAt(row) == 0);
            unique.insert(model.slugAt(row));
        }
        QCOMPARE(unique.size(), 3);
        model.setGrouped(true);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.indexOfGroup(*group) >= 0); // the group and C, by title
        QVERIFY(model.setFranchiseMember(*group, QStringLiteral("b"), false));
        QCOMPARE(model.rowCount(), 3); // one work is still a personal franchise; b stands alone
        model.toggleGroup(*group);
        QCOMPARE(model.rowCount(), 4);
        model.openFranchise(*group);
        QCOMPARE(model.franchise().value(QStringLiteral("works")).toList().size(), 1);
        QVERIFY(model.archive({QStringLiteral("a")}, true));
        QCOMPARE(model.archivedCount(), 1);
        QCOMPARE(model.total(), 2);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.contains(QStringLiteral("a"))); // archived is a retained personal record
        model.setArchivedOnly(true);
        QCOMPARE(model.total(), 1);
        QCOMPARE(model.rowCount(), 2); // group plus its expanded archived member
        model.setGrouped(false);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("a"));
        QVERIFY(model.archive({QStringLiteral("a")}, false));
        QCOMPARE(model.rowCount(), 0);
        model.setArchivedOnly(false);
        QCOMPARE(model.rowCount(), 3);
        model.setSearch(QStringLiteral("B"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("b"));
        model.setSearch(QString());
        QVERIFY(model.removePersonal({QStringLiteral("c")}));
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.contains(QStringLiteral("c")));
        const auto before = repo.franchises();
        QVERIFY(before);
        QVERIFY(!model.createFranchise(QStringLiteral("   "), QStringLiteral("a")));
        QVERIFY(!model.createFranchise(QStringLiteral("Unknown"), QStringLiteral("missing")));
        QCOMPARE(repo.franchises()->size(), before->size());
        QVERIFY(model.createFranchise(QStringLiteral("Custom"), QStringLiteral("b")));
        const auto personal = model.franchise().value(QStringLiteral("id")).toLongLong();
        QVERIFY(personal != *group);
        QCOMPARE(model.memberChoices(personal).size(), 1);
        QCOMPARE(
            model.memberChoices(personal).first().toMap().value(QStringLiteral("slug")).toString(),
            QStringLiteral("a"));
        QVERIFY(model.setFranchiseMember(personal, QStringLiteral("a"), true));
        model.openFranchise(*group);
        QVERIFY(model.franchise().value(QStringLiteral("works")).toList().isEmpty());
        model.openFranchise(personal);
        QCOMPARE(model.franchise().value(QStringLiteral("works")).toList().size(), 2);
        model.setGrouped(true);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.groupAt(0), personal);
        model.setSearch(QStringLiteral("A"));
        QCOMPARE(model.rowCount(), 2); // search opens the matching member under its personal group
        QCOMPARE(model.groupAt(0), personal);
        QCOMPARE(model.slugAt(1), QStringLiteral("a"));
    }

    // What is followed splits by what it waits for: the next episode on disk, or episodes to
    // download. A series seen up to a skipped episode goes on at it.
    void tasksSplitWhatIsOnDiskFromWhatIsMissing() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });
        // ready: 1 seen, 2 on disk, 3 missing. hole: finished, 1 and 3 seen, 2 missing.
        // done: finished and seen.
        const auto add = [&](const QString& slug, api::AnimeStatus status, int count) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = slug;
            record.status = status;
            record.episodeCount = count;
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            QList<api::Episode> episodes;
            for (int number = 1; number <= count; ++number) {
                api::Episode episode;
                episode.id = slug + QString::number(number);
                episode.number = number;
                episodes.append(episode);
            }
            QVERIFY(repo.syncEpisodes(slug, episodes, false));
        };
        add(QStringLiteral("ready"), api::AnimeStatus::Airing, 3);
        add(QStringLiteral("hole"), api::AnimeStatus::Finished, 3);
        add(QStringLiteral("done"), api::AnimeStatus::Finished, 2);
        QVERIFY(repo.setWatchState(QStringLiteral("ready"), 1, library::WatchState::Watched));
        QVERIFY(repo.setEpisodeFile(QStringLiteral("ready"), 2, cache.filePath(QStringLiteral("r2.mkv")), 1));
        QVERIFY(repo.setWatchState(QStringLiteral("hole"), 1, library::WatchState::Watched));
        QVERIFY(repo.setWatchState(QStringLiteral("hole"), 3, library::WatchState::Watched));
        QVERIFY(repo.setWatchedThrough(QStringLiteral("done"), 2));

        const auto hole = repo.entry(QStringLiteral("hole"));
        QVERIFY(hole && *hole);
        QCOMPARE((*hole)->resumeNumber, std::optional<double>(2));

        LibraryModel model(service, downloads, api);
        QCOMPARE(model.readyCount(), 1);
        QCOMPARE(model.featured().value(QStringLiteral("slug")).toString(), QStringLiteral("ready"));
        QCOMPARE(model.downloadCount(), 2);
        QCOMPARE(model.pendingCount(), 2); // ready's 3 and hole's 2

        model.setFilter(LibraryModel::Filter::Watched);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("done"));
        model.setFilter(LibraryModel::Filter::All);
        model.setOnDiskOnly(true);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("ready"));
        QCOMPARE(model.filterCounts().at(static_cast<int>(LibraryModel::Filter::All)), 1);
    }
};

QTEST_GUILESS_MAIN(LibraryModelTest)
#include "library_model_test.moc"
