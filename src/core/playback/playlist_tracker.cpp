#include "playlist_tracker.h"

#include "player.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThreadPool>

namespace animehub::playback {
namespace {

constexpr qint64 kSaveIntervalMs = 5000; // as often as mpv's progress is stored

} // namespace

struct PlaylistTracker::Token {
    PlaylistTracker* tracker;
};

PlaylistTracker::PlaylistTracker(library::LibraryRepository& library, QStringList folders,
                                 Sampler sampler, std::chrono::milliseconds interval,
                                 QObject* parent)
    : QObject(parent), library_(library), folders_(std::move(folders)),
      sampler_(std::move(sampler)), token_(std::make_shared<Token>(Token{this})) {
    timer_.setInterval(interval);
    connect(&timer_, &QTimer::timeout, this, &PlaylistTracker::sample);
    timer_.start();
}

PlaylistTracker::~PlaylistTracker() {
    finish();
}

void PlaylistTracker::finish() {
    if (!token_) {
        return;
    }
    timer_.stop();
    token_.reset(); // a sample still in flight is dropped
    save(true);
}

// The window is asked on a worker: a busy player answers late, never the interface.
void PlaylistTracker::sample() {
    if (sampling_ || !token_) {
        return;
    }
    sampling_ = true;
    QThreadPool::globalInstance()->start(
        [sampler = sampler_, token = std::weak_ptr<Token>(token_)] {
            auto sample = sampler();
            QMetaObject::invokeMethod(QCoreApplication::instance(), [token, sample] {
                if (const auto alive = token.lock()) {
                    alive->tracker->sampling_ = false;
                    alive->tracker->apply(sample);
                }
            });
        });
}

void PlaylistTracker::apply(const std::optional<platform::PlayerWindowSample>& sample) {
    // Stopped, between two files or behind a dialog: what was seen last stands.
    if (!sample) {
        return;
    }
    if (sample->fileName != current_.fileName) {
        save(true); // the file it left, at the last position seen
        current_ = {.fileName = sample->fileName, .episode = episodeFor(sample->fileName)};
    }
    current_.positionMs = sample->positionMs;
    current_.durationMs = sample->durationMs;
    save(false);
}

void PlaylistTracker::save(bool final_save) {
    auto& c = current_;
    if (!c.episode || c.positionMs < 0 || watched_.contains(c.fileName)) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    if (!final_save && c.savedAt.isValid() && c.savedAt.msecsTo(now) < kSaveIntervalMs) {
        return;
    }
    c.savedAt = now;
    const bool watched = countsAsWatched(c.positionMs, c.durationMs);
    const auto stored =
        watched ? library_.setWatchState(c.episode->slug, c.episode->number,
                                         library::WatchState::Watched)
                : library_.setWatchState(c.episode->slug, c.episode->number,
                                         library::WatchState::Watching, c.positionMs, c.durationMs);
    if (!stored) {
        qWarning("playback: %s", qPrintable(stored.error().message));
        return;
    }
    if (watched) {
        watched_.insert(c.fileName);
    }
    emit progressChanged(c.episode->slug, c.episode->number);
}

std::optional<PlaylistTracker::Episode> PlaylistTracker::episodeFor(const QString& file_name) {
    if (const auto it = known_.constFind(file_name); it != known_.cend()) {
        return *it;
    }
    std::optional<Episode> found;
    for (const auto& folder : std::as_const(folders_)) {
        const auto path = QDir(folder).filePath(file_name);
        if (!QFileInfo(path).isFile()) {
            continue;
        }
        if (const auto mappings = library_.episodeFileMappings(path);
            mappings && !mappings->isEmpty()) {
            const auto& [slug, number] = mappings->first();
            found = Episode{.slug = slug, .number = number};
            break;
        }
    }
    known_.insert(file_name, found);
    return found;
}

} // namespace animehub::playback
