#ifndef ANIMEHUB_APP_DOWNLOADS_VIEW_H
#define ANIMEHUB_APP_DOWNLOADS_VIEW_H

#include "download_series_model.h"
#include "downloads/download_manager.h"
#include "library/library_service.h"
#include "playback/player.h"

#include <QTimer>
#include <QtQmlIntegration>

namespace animehub::app {

// The Downloads page, by series as in Steam, and the bar at the bottom of the window: the
// series in line, the completed ones, the download on view and the speed of the session.
class DownloadsView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(DownloadSeriesModel* queue READ queue CONSTANT)
    Q_PROPERTY(DownloadSeriesModel* completed READ completed CONSTANT)

    // Running, queued or due to retry: what goes on by itself. Paused ones wait for the user.
    Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
    Q_PROPERTY(bool anyPaused READ anyPaused NOTIFY summaryChanged)

    // The download on view at the top of the page.
    Q_PROPERTY(Mode headerMode READ headerMode NOTIFY summaryChanged)
    Q_PROPERTY(QString headerSlug READ headerSlug NOTIFY summaryChanged)
    Q_PROPERTY(QString headerTitle READ headerTitle NOTIFY summaryChanged)
    Q_PROPERTY(QString headerEpisode READ headerEpisode NOTIFY summaryChanged)
    Q_PROPERTY(QString headerDetail READ headerDetail NOTIFY summaryChanged)
    Q_PROPERTY(double headerProgress READ headerProgress NOTIFY summaryChanged)
    Q_PROPERTY(QUrl headerArt READ headerArt NOTIFY summaryChanged)

    // The bar at the bottom of the window: shown while something is unfinished.
    Q_PROPERTY(bool barVisible READ barVisible NOTIFY summaryChanged)
    Q_PROPERTY(Mode barMode READ barMode NOTIFY summaryChanged)
    Q_PROPERTY(QString barText READ barText NOTIFY summaryChanged)
    Q_PROPERTY(QString barProblem READ barProblem NOTIFY summaryChanged)
    Q_PROPERTY(double barProgress READ barProgress NOTIFY summaryChanged)

    // Total speed of the last minutes (bytes per second, oldest first) and the session's figures.
    Q_PROPERTY(QList<qreal> speedSamples READ speedSamples NOTIFY speedChanged)
    Q_PROPERTY(QString speedText READ speedText NOTIFY speedChanged)
    Q_PROPERTY(QString peakText READ peakText NOTIFY speedChanged)
    Q_PROPERTY(QString totalText READ totalText NOTIFY speedChanged)

public:
    enum class Mode {
        Idle,    // nothing in line
        Active,  // something transfers, looks for its file or is checked
        Waiting, // nothing runs; something will start or retry by itself
        Paused,  // what is left waits for the user to resume it
        Failed,  // only failed downloads are left
    };
    Q_ENUM(Mode)

    DownloadsView(downloads::DownloadManager& manager, library::LibraryService& library,
                  playback::Player& player, QObject* parent = nullptr);

    [[nodiscard]] DownloadSeriesModel* queue() {
        return &queue_;
    }
    [[nodiscard]] DownloadSeriesModel* completed() {
        return &completed_;
    }

    [[nodiscard]] int activeCount() const {
        return active_count_;
    }
    [[nodiscard]] bool anyPaused() const {
        return any_paused_;
    }

    [[nodiscard]] Mode headerMode() const {
        return header_.mode;
    }
    [[nodiscard]] QString headerSlug() const {
        return header_.slug;
    }
    [[nodiscard]] QString headerTitle() const {
        return header_.title;
    }
    [[nodiscard]] QString headerEpisode() const {
        return header_.episode;
    }
    [[nodiscard]] QString headerDetail() const {
        return header_.detail;
    }
    [[nodiscard]] double headerProgress() const {
        return header_.progress;
    }
    [[nodiscard]] QUrl headerArt() const {
        return header_.art;
    }

    [[nodiscard]] bool barVisible() const {
        return bar_.mode != Mode::Idle;
    }
    [[nodiscard]] Mode barMode() const {
        return bar_.mode;
    }
    [[nodiscard]] QString barText() const {
        return bar_.text;
    }
    [[nodiscard]] QString barProblem() const {
        return bar_.problem;
    }
    [[nodiscard]] double barProgress() const {
        return bar_.progress;
    }

    [[nodiscard]] QList<qreal> speedSamples() const {
        return samples_;
    }
    [[nodiscard]] QString speedText() const;
    [[nodiscard]] QString peakText() const;
    [[nodiscard]] QString totalText() const;

    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    Q_INVOKABLE void retryAllFailed();

    Q_INVOKABLE void pauseSeries(const QString& slug);
    Q_INVOKABLE void resumeSeries(const QString& slug);
    Q_INVOKABLE void retryFailed(const QString& slug);
    Q_INVOKABLE void prioritize(const QString& slug);
    // Drag and drop in "A continuación": the series at `from` goes to `to`.
    Q_INVOKABLE void moveSeries(int from, int to);
    // Stops the series' unfinished episodes and deletes their partial files.
    Q_INVOKABLE void cancelSeries(const QString& slug);
    // Leaves the list; the files stay.
    Q_INVOKABLE void forgetSeries(const QString& slug);
    // Moves the series' downloaded files to the Recycle Bin, then it leaves the list.
    Q_INVOKABLE void trashSeries(const QString& slug);
    // Every completed series leaves the list; the files stay.
    Q_INVOKABLE void clearCompleted();
    Q_INVOKABLE void openSeriesFolder(const QString& slug);
    // Plays the series' next episode to watch among the downloaded ones.
    Q_INVOKABLE void watchNext(const QString& slug);

    Q_INVOKABLE void pause(qint64 id);
    Q_INVOKABLE void resume(qint64 id);
    Q_INVOKABLE void retry(qint64 id);
    Q_INVOKABLE void cancel(qint64 id);
    Q_INVOKABLE void forget(qint64 id);
    Q_INVOKABLE void play(qint64 id);
    Q_INVOKABLE void showFile(qint64 id);

signals:
    void summaryChanged();
    void speedChanged();
    void message(const QString& text, bool problem);

private:
    // What the page needs from the library about a series, read once per change.
    struct SeriesInfo {
        QUrl art;
        std::optional<double> resumeNumber;
        QHash<double, library::EpisodeRecord> episodes;
    };
    struct Header {
        Mode mode = Mode::Idle;
        QString slug;
        QString title;
        QString episode;
        QString detail;
        double progress = 0;
        QUrl art;

        bool operator==(const Header&) const = default;
    };
    struct Bar {
        Mode mode = Mode::Idle;
        QString text;
        QString problem;
        double progress = 0;

        bool operator==(const Bar&) const = default;
    };
    // What the session counter last saw of a task.
    struct Seen {
        downloads::TaskState state = downloads::TaskState::Queued;
        qint64 bytes = 0;
    };

    void rebuild();
    void reorderNow();
    void taskChanged(qint64 id);
    void sample();
    const SeriesInfo& info(const QString& slug);
    [[nodiscard]] std::optional<double> nextToWatch(const QString& slug,
                                                    const QList<downloads::Task>& tasks);
    void playEpisode(const QString& slug, double number, const QString& title);
    void forgetCancelled(const QString& slug);
    [[nodiscard]] QList<downloads::Task> tasksOf(const QString& slug) const;

    downloads::DownloadManager& manager_;
    library::LibraryService& library_;
    playback::Player& player_;
    DownloadSeriesModel queue_;
    DownloadSeriesModel completed_;
    QHash<QString, SeriesInfo> infos_;
    QTimer rebuild_timer_;

    int active_count_ = 0;
    bool any_paused_ = false;
    Header header_;
    Bar bar_;

    QTimer sample_timer_;
    QList<qreal> samples_;
    qint64 speed_ = 0;
    qint64 peak_ = 0;
    qint64 session_bytes_ = 0;
    QHash<qint64, Seen> seen_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_DOWNLOADS_VIEW_H
