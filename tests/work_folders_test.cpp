#include "downloads/download_repository.h"
#include "library/library_repository.h"
#include "library/work_folders.h"
#include "util/file_names.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using namespace animehub::library;
using namespace Qt::StringLiterals;

class WorkFoldersTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> temp_;
    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<LibraryRepository> library_;
    std::unique_ptr<downloads::DownloadRepository> queue_;
    QString root_;

    void addWork(const QString& slug, const QString& title, std::optional<int> year = {}) {
        AnimeRecord anime;
        anime.slug = slug;
        anime.title = title;
        anime.startYear = year;
        QVERIFY(library_->saveSnapshot(anime));
        QVERIFY(library_->setInLibrary(slug, true));
    }

    // An empty placeholder video in `folder` (under the download directory), in the library.
    QString addFile(const QString& slug, const QString& folder, double number) {
        const auto path = root_ + u'/' + folder + u'/' + QStringLiteral("ep %1.mp4").arg(number);
        if (!QDir().mkpath(QFileInfo(path).path()))
            return {};
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.close();
        if (!library_->setEpisodeFile(slug, number, path, 0))
            return {};
        return path;
    }

    QString folderOf(const QString& slug, const QString& title) const {
        const auto folder = library_->workFolder(root_, slug, title);
        return folder ? QFileInfo(*folder).fileName() : u"<error>"_s;
    }

    QString storedFile(const QString& slug, double number) const {
        const auto locations = library_->episodeLocations(slug, number);
        return locations && !locations->isEmpty() ? locations->first().path : QString();
    }

    // The folders' names as the file system spells them.
    QStringList dirs() const {
        return QDir(root_).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    }

    bool hasMarker() const {
        auto query = database_->run(u"SELECT COUNT(*) FROM maintenance"_s);
        return query && query->next() && query->value(0).toInt() == 1;
    }

private slots:
    void init() {
        temp_ = std::make_unique<QTemporaryDir>();
        QVERIFY(temp_->isValid());
        root_ = temp_->filePath(u"Anime"_s);
        QVERIFY(QDir().mkpath(root_));
        auto opened = storage::Database::open(temp_->filePath(u"library.sqlite3"_s));
        QVERIFY(opened);
        database_ = std::move(*opened);
        library_ = std::make_unique<LibraryRepository>(*database_);
        queue_ = std::make_unique<downloads::DownloadRepository>(*database_);
    }

    void cleanup() {
        queue_.reset();
        library_.reset();
        database_.reset();
        temp_.reset();
    }

    void newWorkFolderIsItsCleanTitle() {
        addWork(u"tokyo-revengers-santen-sensou-hen"_s, u"Tokyo Revengers: Santen Sensou-hen"_s);
        QCOMPARE(folderOf(u"tokyo-revengers-santen-sensou-hen"_s,
                          u"Tokyo Revengers: Santen Sensou-hen"_s),
                 u"Tokyo Revengers Santen Sensou-hen"_s);
    }

    // Two works with one clean title never share a folder: the year, then a stable suffix.
    void collidingTitlesGetTheYearThenASuffix() {
        addWork(u"hunter-x-hunter"_s, u"Hunter x Hunter"_s, 1999);
        addWork(u"hunter-x-hunter-2011"_s, u"Hunter x Hunter"_s, 2011);
        addWork(u"hunter-x-hunter-2011-bis"_s, u"Hunter x Hunter"_s, 2011);
        QVERIFY(!addFile(u"hunter-x-hunter"_s, u"Hunter x Hunter"_s, 1).isEmpty());
        QCOMPARE(folderOf(u"hunter-x-hunter-2011"_s, u"Hunter x Hunter"_s),
                 u"Hunter x Hunter (2011)"_s);
        QVERIFY(!addFile(u"hunter-x-hunter-2011"_s, u"Hunter x Hunter (2011)"_s, 1).isEmpty());
        const auto third = folderOf(u"hunter-x-hunter-2011-bis"_s, u"Hunter x Hunter"_s);
        QVERIFY2(third.startsWith(u"Hunter x Hunter (2011) ["_s), qPrintable(third));
        // A folder AnimeHub does not know is not merged into either.
        QVERIFY(QDir(root_).mkpath(u"Dandadan"_s));
        addWork(u"dandadan"_s, u"Dandadan"_s, 2024);
        QCOMPARE(folderOf(u"dandadan"_s, u"Dandadan"_s), u"Dandadan (2024)"_s);
    }

    // AnimeAV1 retitles works: new episodes still go where the earlier ones are.
    void retitleKeepsTheExistingFolder() {
        addWork(u"frieren"_s, u"Sousou no Frieren"_s);
        QVERIFY(!addFile(u"frieren"_s, u"Sousou no Frieren"_s, 1).isEmpty());
        QCOMPARE(folderOf(u"frieren"_s, u"Frieren: Beyond Journey's End"_s),
                 u"Sousou no Frieren"_s);
        // Also a queued episode whose folder does not exist yet.
        addWork(u"bebop"_s, u"Cowboy Bebop"_s);
        QVERIFY(queue_->create(u"bebop"_s, 1, u"Cowboy Bebop"_s, api::Audio::Sub,
                               downloads::TaskOrigin::Manual,
                               root_ + u"/Cowboy Bebop/Cowboy Bebop - 01"_s));
        QCOMPARE(folderOf(u"bebop"_s, u"Cowboy Bebop Remastered"_s), u"Cowboy Bebop"_s);
        // A folder deleted from disk, with only finished history left, is no longer its.
        QVERIFY(QDir(root_ + u"/Sousou no Frieren"_s).removeRecursively());
        QCOMPARE(folderOf(u"frieren"_s, u"Frieren"_s), u"Frieren"_s);
    }

    void migrationRenamesAndUpdatesEveryStoredPath() {
        addWork(u"tougen-anki"_s, u"Tougen Anki: Nikko Kegon no Taki-hen"_s, 2025);
        const auto file = addFile(u"tougen-anki"_s, u"tougen-anki"_s, 1);
        QVERIFY(!file.isEmpty());
        auto finished = queue_->create(u"tougen-anki"_s, 1, u"Tougen Anki"_s, api::Audio::Sub,
                                       downloads::TaskOrigin::Manual, file);
        QVERIFY(finished);
        finished->state = downloads::TaskState::Completed;
        QVERIFY(queue_->save(*finished));
        auto queued = queue_->create(
            u"tougen-anki"_s, 2, u"Tougen Anki"_s, api::Audio::Sub, downloads::TaskOrigin::Manual,
            root_ + u"/tougen-anki/Tougen Anki - 02"_s, downloads::DownloadEngine::JDownloaderLocal,
            u"http://127.0.0.1:3128"_s);
        QVERIFY(queued); // routed to JDownloader, not handed over yet
        // Not AnimeHub's: a folder of no work, another outside the download directory.
        QVERIFY(QDir(root_).mkpath(u"one-piece"_s));
        const auto outside = temp_->filePath(u"Other/tougen-anki"_s);
        QVERIFY(QDir().mkpath(outside));

        const auto result = migrateWorkFolders(*database_, root_);
        QVERIFY2(result, result ? "" : qPrintable(result.error().message));
        QCOMPARE(result->moved.size(), 1);
        QVERIFY(result->skipped.isEmpty());
        QVERIFY(result->done);
        const auto renamed = root_ + u"/Tougen Anki Nikko Kegon no Taki-hen"_s;
        QCOMPARE(result->moved.first().to, renamed);
        QCOMPARE(result->moved.first().files, 1);
        QCOMPARE(result->moved.first().downloads, 2);
        QVERIFY(!QFileInfo::exists(root_ + u"/tougen-anki"_s));
        QVERIFY(QFileInfo::exists(renamed + u"/ep 1.mp4"_s)); // files keep their names
        QCOMPARE(storedFile(u"tougen-anki"_s, 1), renamed + u"/ep 1.mp4"_s);
        QCOMPARE((*library_->episode(u"tougen-anki"_s, 1))->filePath, renamed + u"/ep 1.mp4"_s);
        // The path key follows, so lookups by path find the file.
        QCOMPARE(library_->episodeFileMappings(renamed + u"/EP 1.mp4"_s)->size(), 1);
        QCOMPARE((*queue_->task(finished->id))->targetPath, renamed + u"/ep 1.mp4"_s);
        const auto moved_queued = *queue_->task(queued->id);
        QCOMPARE(moved_queued->targetPath, renamed + u"/Tougen Anki - 02"_s);
        QVERIFY(moved_queued->engineDestination.startsWith(renamed + u"/.jd-"_s));
        QVERIFY(QFileInfo(root_ + u"/one-piece"_s).isDir());
        QVERIFY(QFileInfo(outside).isDir());
        QCOMPARE(folderOf(u"tougen-anki"_s, u"Tougen Anki"_s), QFileInfo(renamed).fileName());

        // Once per library: a second run changes nothing.
        QVERIFY(hasMarker());
        QVERIFY(QDir(root_).mkpath(u"tougen-anki"_s));
        QVERIFY(library_->setEpisodeFile(u"tougen-anki"_s, 3, root_ + u"/tougen-anki/3.mp4"_s, 0));
        const auto again = migrateWorkFolders(*database_, root_);
        QVERIFY(again && again->done && again->moved.isEmpty());
        QVERIFY(QFileInfo(root_ + u"/tougen-anki"_s).isDir());
    }

    // Whatever the new name is taken by stays apart: nothing is overwritten nor merged, and the
    // folder is tried again at the next start.
    void migrationSkipsWhenTheNewNameExists() {
        addWork(u"dandadan"_s, u"Dandadan"_s);
        const auto file = addFile(u"dandadan"_s, u"dandadan"_s, 1);
        addWork(u"kaiju"_s, u"Kaiju No. 8"_s);
        const auto kaiju = addFile(u"kaiju"_s, u"kaiju"_s, 1);
        QVERIFY(QDir(root_).mkpath(u"Kaiju No. 8"_s));
        const auto result = migrateWorkFolders(*database_, root_);
        QVERIFY(result);
        QCOMPARE(result->skipped.size(), 1);
        QCOMPARE(result->skipped.first().slug, u"kaiju"_s);
        QVERIFY(result->skipped.first().retry);
        QVERIFY(!result->done);
        QVERIFY(!hasMarker());
        QCOMPARE(storedFile(u"kaiju"_s, 1), kaiju);
        QVERIFY(QFileInfo::exists(kaiju));
        QVERIFY(QDir(root_ + u"/Kaiju No. 8"_s).isEmpty());
        // Only the case changes for Dandadan: renamed in place on any file system.
        QCOMPARE(result->moved.size(), 1);
        QVERIFY(dirs().contains(u"Dandadan"_s) && !dirs().contains(u"dandadan"_s));
        QCOMPARE(storedFile(u"dandadan"_s, 1), root_ + u"/Dandadan/ep 1.mp4"_s);
        QVERIFY(!file.isEmpty());
        // Once the name is free, the next start finishes the job.
        QVERIFY(QDir(root_).rmdir(u"Kaiju No. 8"_s));
        const auto retried = migrateWorkFolders(*database_, root_);
        QVERIFY(retried && retried->done && retried->moved.size() == 1);
        QCOMPARE(storedFile(u"kaiju"_s, 1), root_ + u"/Kaiju No. 8/ep 1.mp4"_s);
        QVERIFY(hasMarker());
    }

    void migrationSkipsAFolderJDownloaderIsWritingIn() {
        addWork(u"mashle"_s, u"Mashle"_s);
        auto task =
            queue_->create(u"mashle"_s, 1, u"Mashle"_s, api::Audio::Sub,
                           downloads::TaskOrigin::Manual, root_ + u"/mashle/Mashle - 01"_s,
                           downloads::DownloadEngine::JDownloaderLocal, u"http://127.0.0.1:3128"_s);
        QVERIFY(task);
        task->engineSubmission = u"intent"_s;
        task->state = downloads::TaskState::Downloading;
        QVERIFY(queue_->save(*task));
        QVERIFY(QDir().mkpath(task->engineDestination));
        const auto result = migrateWorkFolders(*database_, root_);
        QVERIFY(result && result->moved.isEmpty() && result->skipped.size() == 1);
        QVERIFY(QFileInfo(task->engineDestination).isDir());
        QCOMPARE((*queue_->task(task->id))->targetPath, task->targetPath);
    }

    void migrationRenamesBackWhenTheLibraryCannotBeUpdated() {
        addWork(u"mashle"_s, u"Mashle"_s);
        const auto file = addFile(u"mashle"_s, u"mashle"_s, 1);
        QVERIFY(queue_->create(u"mashle"_s, 2, u"Mashle"_s, api::Audio::Sub,
                               downloads::TaskOrigin::Manual, root_ + u"/mashle/Mashle - 02"_s));
        QVERIFY(database_->run(u"CREATE TRIGGER refuse BEFORE UPDATE ON download BEGIN "
                               "SELECT RAISE(ABORT, 'refused'); END"_s));
        const auto result = migrateWorkFolders(*database_, root_);
        QVERIFY(result);
        QVERIFY(result->moved.isEmpty());
        QCOMPARE(result->skipped.size(), 1);
        QVERIFY(result->skipped.first().retry);
        QVERIFY(result->skipped.first().reason.contains(u"refused"_s));
        QVERIFY(QFileInfo::exists(file));
        QCOMPARE(dirs(), QStringList{u"mashle"_s});
        QCOMPARE(storedFile(u"mashle"_s, 1), file); // the library update was rolled back too
        QVERIFY(!hasMarker());
    }

    // The earliest of two works with one title keeps it; the other one gets its year.
    void migrationKeepsCollidingWorksApart() {
        addWork(u"hunter-x-hunter-2011"_s, u"Hunter x Hunter"_s, 2011);
        addWork(u"hunter-x-hunter"_s, u"Hunter x Hunter"_s, 1999);
        QVERIFY(!addFile(u"hunter-x-hunter-2011"_s, u"hunter-x-hunter-2011"_s, 1).isEmpty());
        QVERIFY(!addFile(u"hunter-x-hunter"_s, u"hunter-x-hunter"_s, 1).isEmpty());
        const auto result = migrateWorkFolders(*database_, root_);
        QVERIFY(result && result->done && result->moved.size() == 2);
        QCOMPARE(storedFile(u"hunter-x-hunter"_s, 1), root_ + u"/Hunter x Hunter/ep 1.mp4"_s);
        QCOMPARE(storedFile(u"hunter-x-hunter-2011"_s, 1),
                 root_ + u"/Hunter x Hunter (2011)/ep 1.mp4"_s);
    }
};

QTEST_GUILESS_MAIN(WorkFoldersTest)
#include "work_folders_test.moc"
