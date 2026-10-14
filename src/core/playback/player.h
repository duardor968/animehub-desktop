#ifndef ANIMEHUB_CORE_PLAYBACK_PLAYER_H
#define ANIMEHUB_CORE_PLAYBACK_PLAYER_H

#include "library/library_repository.h"

#include <QDateTime>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <expected>
#include <functional>
#include <memory>

namespace animehub::playback {

enum class PlayerKind { Bundled, System };

struct PlayerSettings {
    PlayerKind kind = PlayerKind::Bundled;
    QString mpvExecutable;   // the bundled mpv
    QString configDirectory; // mpv's own settings, separate from any user mpv setup
};

struct PlayRequest {
    QString slug;
    double number = 0;
    QString filePath;
    QString title; // shown in the player's window
    qint64 startMs = 0;
};

// Whether a stop at `position` of `duration` counts as watched: the last 10 %, or the last two
// minutes of an episode longer than ten (ending and preview).
[[nodiscard]] bool countsAsWatched(qint64 position_ms, qint64 duration_ms);

// Plays downloaded episodes outside AnimeHub. The bundled mpv reports its position over IPC, so
// progress and "watched" are exact; the system player only marks the episode watched when it
// opens. Closing AnimeHub never stops a video.
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
    void handleMessage(const QByteArray& line);
    void save(bool final_save);
    void endSession();

    library::LibraryRepository& library_;
    std::function<PlayerSettings()> settings_;
    std::function<bool(const QString&)> open_with_system_;
    std::unique_ptr<Session> session_;
};

} // namespace animehub::playback

#endif // ANIMEHUB_CORE_PLAYBACK_PLAYER_H
