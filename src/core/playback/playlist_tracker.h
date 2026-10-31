#ifndef ANIMEHUB_CORE_PLAYBACK_PLAYLIST_TRACKER_H
#define ANIMEHUB_CORE_PLAYBACK_PLAYLIST_TRACKER_H

#include "library/library_repository.h"
#include "platform/player_window.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace animehub::playback {

// Follows a player that goes through a list of files on its own (PotPlayer), by sampling what
// its window shows: each library episode it plays keeps its position as mpv's do and is marked
// watched once, when it reaches its end. Files outside the library are ignored.
class PlaylistTracker final : public QObject {
    Q_OBJECT

public:
    using Sampler = std::function<std::optional<platform::PlayerWindowSample>()>;

    // `folders`: where the files the player got are; a library file it plays from there counts,
    // whether AnimeHub listed it or the player added it. `sampler` runs off this thread, one
    // sample at a time, every `interval`.
    PlaylistTracker(library::LibraryRepository& library, QStringList folders, Sampler sampler,
                    std::chrono::milliseconds interval, QObject* parent = nullptr);
    ~PlaylistTracker() override;

    // Saves the episode on view one last time and stops looking.
    void finish();

signals:
    // The library state of this episode changed (progress, watched).
    void progressChanged(const QString& slug, double number);

private:
    struct Episode {
        QString slug;
        double number = 0;
    };
    struct Current {
        QString fileName;
        std::optional<Episode> episode;
        qint64 positionMs = -1;
        qint64 durationMs = 0;
        QDateTime savedAt;
    };
    struct Token; // what a sample in flight comes back to, if the tracker still exists

    void sample();
    void apply(const std::optional<platform::PlayerWindowSample>& sample);
    void save(bool final_save);
    [[nodiscard]] std::optional<Episode> episodeFor(const QString& file_name);

    library::LibraryRepository& library_;
    QStringList folders_;
    Sampler sampler_;
    QTimer timer_;
    std::shared_ptr<Token> token_;
    bool sampling_ = false;
    Current current_;
    QHash<QString, std::optional<Episode>> known_;
    QSet<QString> watched_; // files already marked watched in this session
};

} // namespace animehub::playback

#endif // ANIMEHUB_CORE_PLAYBACK_PLAYLIST_TRACKER_H
