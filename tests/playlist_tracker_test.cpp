#include "playback/player.h"
#include "playback/playlist_tracker.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <mutex>

using namespace animehub;

namespace {

const auto kSlug = QStringLiteral("black-clover-2nd-season");

QString fileName(int number) {
    return QStringLiteral("Black Clover 2nd Season - %1.mp4").arg(number, 2, 10, QLatin1Char('0'));
}

// What PotPlayer's window shows, one sample per look; nothing once the list is over.
class FakeWindow {
public:
    void push(std::optional<platform::PlayerWindowSample> sample) {
        const std::scoped_lock lock(mutex_);
        samples_.append(std::move(sample));
    }
    std::optional<platform::PlayerWindowSample> next() {
        const std::scoped_lock lock(mutex_);
        ++looks_;
        return samples_.isEmpty() ? std::nullopt : samples_.takeFirst();
    }
    int looks() {
        const std::scoped_lock lock(mutex_);
        return looks_;
    }
    bool drained() {
        const std::scoped_lock lock(mutex_);
        return samples_.isEmpty();
    }

private:
    std::mutex mutex_;
    QList<std::optional<platform::PlayerWindowSample>> samples_;
    int looks_ = 0;
};

platform::PlayerWindowSample at(int number, qint64 position, qint64 duration = 1'430'000) {
    return {.fileName = fileName(number), .positionMs = position, .durationMs = duration};
}

} // namespace

class PlaylistTrackerTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> root_;
    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<library::LibraryRepository> library_;
    QString folder_;

    library::EpisodeRecord episode(double number) const {
        return **library_->episode(kSlug, number);
    }

private slots:
    void init() {
        root_ = std::make_unique<QTemporaryDir>();
        auto opened = storage::Database::open(root_->filePath(QStringLiteral("library.sqlite3")));
        QVERIFY(opened);
        database_ = std::move(*opened);
        library_ = std::make_unique<library::LibraryRepository>(*database_);
        library::AnimeRecord anime;
        anime.slug = kSlug;
        anime.title = QStringLiteral("Black Clover 2nd Season");
        QVERIFY(library_->saveSnapshot(anime));
        folder_ = root_->filePath(kSlug);
        QVERIFY(QDir().mkpath(folder_));
        for (const int number : {1, 2, 3}) {
            const auto path = QDir(folder_).filePath(fileName(number));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("video");
            file.close();
            QVERIFY(library_->setEpisodeFile(kSlug, number, path, 5));
        }
        // In the folder, not in the library: the player may list it, AnimeHub ignores it.
        QFile other(QDir(folder_).filePath(QStringLiteral("extra.mp4")));
        QVERIFY(other.open(QIODevice::WriteOnly));
    }

    void cleanup() {
        library_.reset();
        database_.reset();
        root_.reset();
    }

    // The probe's case: PotPlayer goes from episode 1 to 2 by itself. Episode 1 is watched once
    // it reaches its end, episode 2 keeps where it was left, and what is not in the library or
    // not playing (the switch, a dialog) changes nothing.
    void followsTheListAsThePlayerGoesThroughIt() {
        FakeWindow window;
        window.push(at(1, 1'418'000));
        window.push(std::nullopt); // stopped while switching
        window.push(at(1, 1'429'818));
        window.push(at(2, 333));
        window.push(at(1, 1'000)); // back to episode 1: already watched, it stays so
        window.push(at(2, 20'000));
        window.push(platform::PlayerWindowSample{
            .fileName = QStringLiteral("extra.mp4"), .positionMs = 5'000, .durationMs = 60'000});
        window.push(at(2, 61'000));
        playback::PlaylistTracker tracker(
            *library_, {folder_}, [&window] { return window.next(); },
            std::chrono::milliseconds(10));
        QSignalSpy changed(&tracker, &playback::PlaylistTracker::progressChanged);
        QTRY_VERIFY_WITH_TIMEOUT(window.drained(), 5000);
        const int looks = window.looks();
        QTRY_VERIFY(window.looks() > looks); // the last sample was applied
        tracker.finish();

        QCOMPARE(episode(1).state, library::WatchState::Watched);
        QCOMPARE(episode(2).state, library::WatchState::Watching);
        QCOMPARE(episode(2).positionMs, 61'000); // the last position seen, on finishing
        QCOMPARE(episode(2).durationMs, 1'430'000);
        QCOMPARE(episode(3).state, library::WatchState::Unwatched);
        for (const auto& signal : changed) {
            QCOMPARE(signal.at(0).toString(), kSlug);
        }

        // Finished, it no longer looks at the window.
        const int after = window.looks();
        QTest::qWait(100);
        QVERIFY(window.looks() <= after + 1);
    }

    // A window that never shows a library file leaves the library as it was.
    void ignoresFilesOutsideTheLibrary() {
        FakeWindow window;
        window.push(platform::PlayerWindowSample{
            .fileName = QStringLiteral("extra.mp4"), .positionMs = 59'000, .durationMs = 60'000});
        playback::PlaylistTracker tracker(
            *library_, {folder_}, [&window] { return window.next(); },
            std::chrono::milliseconds(10));
        QSignalSpy changed(&tracker, &playback::PlaylistTracker::progressChanged);
        QTRY_VERIFY_WITH_TIMEOUT(window.drained(), 5000);
        tracker.finish();
        QCOMPARE(changed.size(), 0);
        for (const int number : {1, 2, 3}) {
            QCOMPARE(episode(number).state, library::WatchState::Unwatched);
        }
    }

    // A new playback replaces the PotPlayer AnimeHub started: where it was is saved first, then
    // that process, and only that one, is asked to close. One that already ended is left alone.
    void playingAnotherEpisodeClosesThePotPlayerItStarted() {
        FakeWindow window;
        window.push(at(1, 600'000));
        QList<qint64> started;
        QList<qint64> closed;
        qint64 saved_when_closed = -1;
        std::function<void()> last_exit;
        const playback::PotPlayerControl control{
            .start = [&](const QString&, const QStringList&) -> std::optional<qint64> {
                started.append(4000 + started.size());
                return started.last();
            },
            .sample = [&window](qint64) { return window.next(); },
            .watchExit =
                [&](qint64, std::function<void()> exited) {
                    last_exit = std::move(exited);
                    return std::make_unique<QObject>();
                },
            .close =
                [&](qint64 pid) {
                    saved_when_closed = episode(1).positionMs;
                    closed.append(pid);
                }};
        playback::Player player(
            *library_,
            [&] {
                return playback::PlayerSettings{.kind = playback::PlayerKind::PotPlayer,
                                                .potPlayerExecutable =
                                                    QStringLiteral("PotPlayerMini64.exe"),
                                                .potPlayer = control};
            },
            [](const QString&) { return false; });

        QVERIFY(player.play({.slug = kSlug, .number = 1, .filePath = episode(1).filePath}));
        QTRY_VERIFY_WITH_TIMEOUT(window.drained(), 5000);
        const int looks = window.looks();
        QTRY_VERIFY_WITH_TIMEOUT(window.looks() > looks, 5000); // the sample was applied
        QVERIFY(player.play({.slug = kSlug, .number = 2, .filePath = episode(2).filePath}));
        QCOMPARE(closed, QList<qint64>{4000});
        QCOMPARE(saved_when_closed, 600'000);
        QCOMPARE(episode(1).state, library::WatchState::Watching);
        QCOMPARE(started, (QList<qint64>{4000, 4001}));

        // The second one is closed by the person: nothing is left to close.
        last_exit();
        QVERIFY(!player.isTracking());
        QVERIFY(player.play({.slug = kSlug, .number = 3, .filePath = episode(3).filePath}));
        QCOMPARE(closed, QList<qint64>{4000});
    }

    // What PotPlayer is started with: its own process at the minute left, the episode and then
    // the later ones on disk not watched yet. Nothing is started: the launch is only data.
    void startsPotPlayerWithTheEpisodesStillToWatch() {
        QVERIFY(library_->setWatchState(kSlug, 2, library::WatchState::Watched));
        const auto launch = playback::potPlayerLaunch(
            *library_,
            {.slug = kSlug, .number = 1, .filePath = episode(1).filePath, .startMs = 1'418'000});
        QCOMPARE(launch.arguments,
                 (QStringList{QStringLiteral("/new"), QStringLiteral("/seek=00:23:38"),
                              QDir::toNativeSeparators(episode(1).filePath),
                              QDir::toNativeSeparators(episode(3).filePath)}));
        QCOMPARE(launch.folders, QStringList{folder_});
    }
};

QTEST_GUILESS_MAIN(PlaylistTrackerTest)
#include "playlist_tracker_test.moc"
