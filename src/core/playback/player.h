#ifndef ANIMEHUB_CORE_PLAYBACK_PLAYER_H
#define ANIMEHUB_CORE_PLAYBACK_PLAYER_H

#include "library/library_repository.h"
#include "playlist_tracker.h"

#include <QDateTime>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <expected>
#include <functional>
#include <memory>

namespace animehub::playback {

enum class PlayerKind { Bundled, PotPlayer, System };

// How a followed PotPlayer is started, looked at and closed: the system's
// (platformPotPlayer()) in the app, fakes in tests.
struct PotPlayerControl {
    // Its process id, or none when it could not start.
    std::function<std::optional<qint64>(const QString& program, const QStringList& arguments)>
        start;
    std::function<std::optional<platform::PlayerWindowSample>(qint64 pid)> sample;
    std::function<std::unique_ptr<QObject>(qint64 pid, std::function<void()> exited)> watchExit;
    // Only ever for a process `start` returned and still running.
    std::function<void(qint64 pid)> close;
};
[[nodiscard]] PotPlayerControl platformPotPlayer();

struct PlayerSettings {
    PlayerKind kind = PlayerKind::Bundled;
    QString mpvExecutable;   // the bundled mpv
    QString configDirectory; // mpv's own settings, separate from any user mpv setup
    QString potPlayerExecutable;
    PotPlayerControl potPlayer;
    // The program the system opens a file type with (".mp4" -> its executable); unset, the
    // system's player is only ever opened, never followed.
    std::function<QString(const QString& suffix)> systemApplication;
};

struct PlayRequest {
    QString slug;
    double number = 0;
    QString filePath;
    QString title; // shown in the player's window
    qint64 startMs = 0;
};

// How PotPlayer is started for `request`: in its own process ("/new"), at the start position,
// with the episode and then the next ones on disk not watched yet, in order, which it goes on
// to by itself. `folders` holds where those files are, for following its window.
struct PotPlayerLaunch {
    QStringList arguments;
    QStringList folders;
};
[[nodiscard]] PotPlayerLaunch potPlayerLaunch(library::LibraryRepository& library,
                                              const PlayRequest& request);

// Whether a stop at `position` of `duration` counts as watched: the last 10 %, or the last two
// minutes of an episode longer than ten (ending and preview).
[[nodiscard]] bool countsAsWatched(qint64 position_ms, qint64 duration_ms);

// Plays downloaded episodes outside AnimeHub. The bundled mpv reports its position over IPC, so
// progress and "watched" are exact. PotPlayer, chosen or the system's player for the file, gets
// the episode and the next unwatched ones on disk as its list, and its window is followed while
// it goes through them. Any other system player only marks the episode watched when it opens.
// Closing AnimeHub never stops a video.
class Player final : public QObject {
    Q_OBJECT

public:
    // `open_with_system` hands a file to the operating system's default application.
    Player(library::LibraryRepository& library, std::function<PlayerSettings()> settings,
           std::function<bool(const QString&)> open_with_system, QObject* parent = nullptr);
    ~Player() override;

    std::expected<void, QString> play(const PlayRequest& request);
    [[nodiscard]] bool isTracking() const;

signals:
    // The library state of this episode changed (progress, watched).
    void progressChanged(const QString& slug, double number);

private:
    struct Session;

    std::expected<void, QString> playWithMpv(const PlayRequest& request,
                                             const PlayerSettings& settings);
    std::expected<void, QString> playWithPotPlayer(const PlayRequest& request,
                                                   const PlayerSettings& settings,
                                                   const QString& executable);
    // Stops following the previous playback, its progress saved; the player stays open.
    void endPlaylist();
    // A new playback replaces the PotPlayer AnimeHub started: where it was is saved, then it
    // closes as when its window is closed.
    void closePlaylist();
    void handleMessage(const QByteArray& line);
    void save(bool final_save);
    void endSession();

    library::LibraryRepository& library_;
    std::function<PlayerSettings()> settings_;
    std::function<bool(const QString&)> open_with_system_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<PlaylistTracker> playlist_;
    std::unique_ptr<QObject> playlist_exit_;
    qint64 playlist_pid_ = 0;
    std::function<void(qint64)> playlist_close_;
};

} // namespace animehub::playback

#endif // ANIMEHUB_CORE_PLAYBACK_PLAYER_H
