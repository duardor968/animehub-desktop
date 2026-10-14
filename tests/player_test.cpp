#include "playback/player.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using namespace animehub::playback;

class PlayerTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir_;
    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<library::LibraryRepository> library_;
    QString clip_;

    // mpv without a window or sound: the session is followed exactly as on screen.
    QString headlessConfig(const QString& extra = {}) {
        const auto path =
            dir_->filePath(QStringLiteral("mpv-%1").arg(QRandomGenerator::global()->generate()));
        QDir().mkpath(path);
        QFile conf(path + QStringLiteral("/mpv.conf"));
        if (!conf.open(QIODevice::WriteOnly)) {
            qFatal("cannot write mpv.conf");
        }
        conf.write("vo=null\nao=null\n" + extra.toUtf8());
        return path;
    }

    library::WatchState stateOf(double number) const {
        const auto episode = library_->episode(QStringLiteral("clip"), number);
        return episode && *episode ? (*episode)->state : library::WatchState::Unwatched;
    }

private slots:
    void init() {
        dir_ = std::make_unique<QTemporaryDir>();
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        database_ = std::move(*opened);
        library_ = std::make_unique<library::LibraryRepository>(*database_);
        library::AnimeRecord anime;
        anime.slug = QStringLiteral("clip");
        anime.title = QStringLiteral("Clip");
        QVERIFY(library_->saveSnapshot(anime));
        clip_ = dir_->filePath(QStringLiteral("clip.mp4"));
        QVERIFY(QFile::copy(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/media/clip.mp4"), clip_));
    }

    void cleanup() {
        library_.reset();
        database_.reset();
        dir_.reset();
    }

    void decidesWhenAnEpisodeCountsAsWatched() {
        QVERIFY(!countsAsWatched(0, 1'440'000));
        QVERIFY(!countsAsWatched(600'000, 1'440'000));
        QVERIFY(countsAsWatched(1'300'000, 1'440'000)); // the last two minutes
        QVERIFY(countsAsWatched(95'000, 100'000));      // the last 10 % of a short clip
        QVERIFY(!countsAsWatched(80'000, 100'000));
        QVERIFY(!countsAsWatched(500, 0));
    }

    void marksTheEpisodeWatchedWhenMpvReachesTheEnd() {
        const auto config = headlessConfig();
        Player player(*library_,
                      [&] {
                          return PlayerSettings{.kind = PlayerKind::Bundled,
                                                .mpvExecutable =
                                                    QStringLiteral(ANIMEHUB_MPV_EXECUTABLE),
                                                .configDirectory = config};
                      },
                      {});
        QSignalSpy progress(&player, &Player::progressChanged);
        QVERIFY(player.play({.slug = QStringLiteral("clip"),
                             .number = 1,
                             .filePath = clip_,
                             .title = QStringLiteral("Clip · Episodio 1")}));
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(1), library::WatchState::Watched, 20'000);
        QVERIFY(progress.size() >= 1);
        QTRY_VERIFY_WITH_TIMEOUT(!player.isTracking(), 5000); // mpv quit at the end
    }

    void savesTheMinuteWhenPlaybackStopsEarly() {
        // A few loops, then mpv ends by itself (it is detached: the test cannot close it).
        const auto config = headlessConfig(QStringLiteral("loop-file=2\n"));
        Player player(*library_,
                      [&] {
                          return PlayerSettings{.kind = PlayerKind::Bundled,
                                                .mpvExecutable =
                                                    QStringLiteral(ANIMEHUB_MPV_EXECUTABLE),
                                                .configDirectory = config};
                      },
                      {});
        QVERIFY(player.play({.slug = QStringLiteral("clip"),
                             .number = 1,
                             .filePath = clip_,
                             .title = QStringLiteral("Clip"),
                             .startMs = 1000}));
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(1), library::WatchState::Watching, 20'000);
        // Playing another episode closes the first one with its progress saved.
        QVERIFY(player.play({.slug = QStringLiteral("clip"),
                             .number = 2,
                             .filePath = clip_,
                             .title = QStringLiteral("Clip")}));
        const auto first = library_->episode(QStringLiteral("clip"), 1);
        QCOMPARE((*first)->state, library::WatchState::Watching);
        QVERIFY2((*first)->positionMs >= 1000, qPrintable(QString::number((*first)->positionMs)));
        QVERIFY((*first)->durationMs >= 3900);
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(2), library::WatchState::Watching, 20'000);
    }

    void theSystemPlayerMarksTheEpisodeWatchedWhenItOpens() {
        QString opened;
        Player player(
            *library_, [] { return PlayerSettings{.kind = PlayerKind::System}; },
            [&](const QString& path) {
                opened = path;
                return true;
            });
        QVERIFY(player.play({.slug = QStringLiteral("clip"), .number = 3, .filePath = clip_}));
        QCOMPARE(opened, clip_);
        QCOMPARE(stateOf(3), library::WatchState::Watched);

        Player refusing(
            *library_, [] { return PlayerSettings{.kind = PlayerKind::System}; },
            [](const QString&) { return false; });
        QVERIFY(!refusing.play({.slug = QStringLiteral("clip"), .number = 4, .filePath = clip_}));
        QCOMPARE(stateOf(4), library::WatchState::Unwatched);
    }

    void refusesAMissingFile() {
        Player player(
            *library_, [] { return PlayerSettings{.kind = PlayerKind::System}; },
            [](const QString&) { return true; });
        const auto result = player.play({.slug = QStringLiteral("clip"),
                                         .number = 1,
                                         .filePath = dir_->filePath(QStringLiteral("gone.mp4"))});
        QVERIFY(!result);
        QCOMPARE(result.error(), QStringLiteral("El archivo ya no está en el disco."));
    }
};

QTEST_GUILESS_MAIN(PlayerTest)
#include "player_test.moc"
