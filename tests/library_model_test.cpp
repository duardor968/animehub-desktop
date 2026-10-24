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
        model.setFilter(LibraryModel::Filter::AllAnime);
        QCOMPARE(model.rowCount(), 3);
        QSet<QString> unique;
        for (int row = 0; row < model.rowCount(); ++row) {
            QVERIFY(model.groupAt(row) == 0);
            unique.insert(model.slugAt(row));
        }
        QCOMPARE(unique.size(), 3);
        model.setFilter(LibraryModel::Filter::Franchises);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.groupAt(0), *group);
        QVERIFY(model.setFranchiseMember(*group, QStringLiteral("b"), false));
        QCOMPARE(model.rowCount(), 1); // one work is still an explicitly personal franchise
        model.setFilter(LibraryModel::Filter::All);
        QCOMPARE(model.rowCount(), 3);
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
        model.setFilter(LibraryModel::Filter::AllAnime);
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
        model.setFilter(LibraryModel::Filter::Franchises);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.groupAt(0), personal);
        model.setFilter(LibraryModel::Filter::All);
        model.setSearch(QStringLiteral("A"));
        QCOMPARE(model.rowCount(), 2); // search opens the matching member under its personal group
        QCOMPARE(model.groupAt(0), personal);
        QCOMPARE(model.slugAt(1), QStringLiteral("a"));
        const auto candidate = [&](const QString& slug) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = slug.toUpper();
            if (!repo.saveSnapshot(record))
                return false;
            return db
                ->run(QStringLiteral("INSERT INTO franchise_candidate(slug, reason) VALUES "
                                     "(?, 'multiple_groups')"),
                      {slug})
                .has_value();
        };
        QVERIFY(candidate(QStringLiteral("d")));
        QCOMPARE(model.pendingCandidates().size(), 1);
        QVERIFY(!model.resolveCandidate(QStringLiteral("d"), -100));
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(model.pendingCandidates().size(), 1);
        QVERIFY(!model.contains(QStringLiteral("d")));
        QVERIFY(model.resolveCandidate(QStringLiteral("d"), personal));
        QVERIFY(model.error().isEmpty());
        QVERIFY(model.pendingCandidates().isEmpty());
        QVERIFY(model.contains(QStringLiteral("d")));
        QVERIFY(candidate(QStringLiteral("e")));
        QVERIFY(model.dismissCandidate(QStringLiteral("e")));
        QVERIFY(model.pendingCandidates().isEmpty());
        QVERIFY(!model.contains(QStringLiteral("e")));
        QVERIFY(candidate(QStringLiteral("f")));
        QVERIFY(model.createCandidateFranchise(QStringLiteral("f"), QStringLiteral("New group")));
        QVERIFY(model.pendingCandidates().isEmpty());
        QVERIFY(model.contains(QStringLiteral("f")));
        QCOMPARE(model.franchise().value(QStringLiteral("title")).toString(),
                 QStringLiteral("New group"));
        QCOMPARE(model.franchiseChoices().size(), 3);
    }
};

QTEST_GUILESS_MAIN(LibraryModelTest)
#include "library_model_test.moc"
