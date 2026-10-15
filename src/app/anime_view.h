#ifndef ANIMEHUB_APP_ANIME_VIEW_H
#define ANIMEHUB_APP_ANIME_VIEW_H

#include "app_settings.h"
#include "card_model.h"
#include "downloads/download_manager.h"
#include "episode_model.h"
#include "jdownloader_view.h"
#include "library/library_service.h"
#include "playback/player.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration>

#include <functional>

namespace animehub::app {

struct Services {
    api::ApiClient& api;
    library::LibraryService& library;
    downloads::DownloadManager& downloads;
    JDownloaderView& jdownloader;
    playback::Player& player;
    AppSettings& settings;
};

// One anime's page: its details, the user's flags, the main action, the user's progress, related
// anime and the episode list. Works the same for library and catalog anime; watching or
// downloading something keeps the anime in the library.
class AnimeView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppController")

    Q_PROPERTY(QString slug READ slug NOTIFY slugChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString alternativeTitle READ alternativeTitle NOTIFY changed)
    Q_PROPERTY(QString synopsis READ synopsis NOTIFY changed)
    Q_PROPERTY(QUrl posterUrl READ posterUrl NOTIFY changed)
    Q_PROPERTY(QUrl backdropUrl READ backdropUrl NOTIFY changed)
    // "9,1 (48 mil)": score and votes; empty without a score.
    Q_PROPERTY(QString score READ score NOTIFY changed)
    // "TV Anime · 2023 · Finalizado · 28 episodios"
    Q_PROPERTY(QString facts READ facts NOTIFY changed)
    Q_PROPERTY(QString genres READ genres NOTIFY changed)
    // When the next episode airs, in local time: "jueves 22:00"; empty if unknown.
    Q_PROPERTY(QString nextAiring READ nextAiring NOTIFY changed)
    Q_PROPERTY(QUrl trailerUrl READ trailerUrl NOTIFY changed)
    // A film or any work of one episode: no episode list.
    Q_PROPERTY(bool single READ single NOTIFY changed)
    // Airing or upcoming: new episodes can still arrive.
    Q_PROPERTY(bool airing READ airing NOTIFY changed)
    Q_PROPERTY(bool inLibrary READ inLibrary NOTIFY changed)
    Q_PROPERTY(bool favorite READ favorite NOTIFY changed)
    Q_PROPERTY(bool notify READ notify NOTIFY changed)
    Q_PROPERTY(bool autoDownload READ autoDownload NOTIFY changed)

    Q_PROPERTY(PrimaryAction primaryAction READ primaryAction NOTIFY changed)
    Q_PROPERTY(QString primaryText READ primaryText NOTIFY changed)
    // Share of the series' current downloads already on disk (Downloading, Paused).
    Q_PROPERTY(double primaryProgress READ primaryProgress NOTIFY changed)
    // Episodes neither on disk nor on their way; of those, the unwatched ones.
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(int unwatchedMissingCount READ unwatchedMissingCount NOTIFY changed)
    Q_PROPERTY(double firstMissing READ firstMissing NOTIFY changed)
    Q_PROPERTY(double firstNumber READ firstNumber NOTIFY changed)
    Q_PROPERTY(double lastNumber READ lastNumber NOTIFY changed)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY changed) // unfinished ones
    Q_PROPERTY(bool watchedOnDisk READ watchedOnDisk NOTIFY changed)

    // "Tu progreso"
    Q_PROPERTY(bool showProgress READ showProgress NOTIFY changed)
    Q_PROPERTY(QString watchedText READ watchedText NOTIFY changed) // "12 de 28"
    Q_PROPERTY(double watchedProgress READ watchedProgress NOTIFY changed)
    Q_PROPERTY(QString diskText READ diskText NOTIFY changed) // "2 de 28 · 700 MB"
    // The series' downloads: some transferring or waiting their turn, or all of them paused.
    Q_PROPERTY(bool downloadRunning READ downloadRunning NOTIFY changed)
    Q_PROPERTY(bool downloadPaused READ downloadPaused NOTIFY changed)
    Q_PROPERTY(bool downloadWaiting READ downloadWaiting NOTIFY changed)
    Q_PROPERTY(QString downloadText READ downloadText NOTIFY changed) // "3 de 12"
    Q_PROPERTY(int failedCount READ failedCount NOTIFY changed)
    // The episode to watch next: number (< 0 when everything is watched), title, image, state.
    Q_PROPERTY(double nextNumber READ nextNumber NOTIFY changed)
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY changed)
    Q_PROPERTY(QUrl nextImage READ nextImage NOTIFY changed)
    Q_PROPERTY(double nextProgress READ nextProgress NOTIFY changed)
    Q_PROPERTY(QString nextState READ nextState NOTIFY changed)
    Q_PROPERTY(bool nextHasFile READ nextHasFile NOTIFY changed)
    Q_PROPERTY(bool nextBusy READ nextBusy NOTIFY changed) // its download is under way
    Q_PROPERTY(bool nextFailed READ nextFailed NOTIFY changed)

    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString downloadFolder READ downloadFolder NOTIFY changed)
    Q_PROPERTY(bool canSendToJDownloader READ canSendToJDownloader NOTIFY changed)
    Q_PROPERTY(EpisodeModel* episodes READ episodes CONSTANT)
    Q_PROPERTY(CardModel* related READ related CONSTANT)

public:
    // What the main button does: watch the next episode, download what is missing, or follow
    // the series' downloads in progress.
    enum class PrimaryAction { None, Play, Download, Downloading, Paused };
    Q_ENUM(PrimaryAction)

    AnimeView(Services services, QObject* parent = nullptr);

    // Shows `slug`; `hint` (a catalog card) paints the header before the API answers.
    void open(const QString& slug, const library::AnimeRecord& hint = {});

    [[nodiscard]] QString slug() const {
        return slug_;
    }
    [[nodiscard]] bool loaded() const {
        return record_.has_value();
    }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString alternativeTitle() const;
    [[nodiscard]] QString synopsis() const;
    [[nodiscard]] QUrl posterUrl() const;
    [[nodiscard]] QUrl backdropUrl() const;
    [[nodiscard]] QString score() const;
    [[nodiscard]] QString facts() const;
    [[nodiscard]] QString genres() const;
    [[nodiscard]] QString nextAiring() const;
    [[nodiscard]] QUrl trailerUrl() const;
    [[nodiscard]] bool single() const;
    [[nodiscard]] bool airing() const;
    [[nodiscard]] bool inLibrary() const;
    [[nodiscard]] bool favorite() const;
    [[nodiscard]] bool notify() const;
    [[nodiscard]] bool autoDownload() const;

    [[nodiscard]] PrimaryAction primaryAction() const {
        return summary_.primary;
    }
    [[nodiscard]] QString primaryText() const;
    [[nodiscard]] double primaryProgress() const {
        return summary_.batchProgress;
    }
    [[nodiscard]] int missingCount() const {
        return static_cast<int>(summary_.missing.size());
    }
    [[nodiscard]] int unwatchedMissingCount() const {
        return static_cast<int>(summary_.unwatchedMissing.size());
    }
    [[nodiscard]] double firstMissing() const {
        return summary_.missing.isEmpty() ? firstNumber() : summary_.missing.first();
    }
    [[nodiscard]] double firstNumber() const;
    [[nodiscard]] double lastNumber() const;
    [[nodiscard]] bool hasDownloads() const {
        return summary_.batchTotal > summary_.batchDone;
    }
    [[nodiscard]] bool watchedOnDisk() const {
        return summary_.watchedOnDisk;
    }

    [[nodiscard]] bool showProgress() const;
    [[nodiscard]] QString watchedText() const;
    [[nodiscard]] double watchedProgress() const;
    [[nodiscard]] QString diskText() const;
    [[nodiscard]] bool downloadRunning() const {
        return summary_.running;
    }
    [[nodiscard]] bool downloadPaused() const {
        return summary_.paused;
    }
    [[nodiscard]] bool downloadWaiting() const {
        return summary_.retryAt.isValid();
    }
    [[nodiscard]] QString downloadText() const;
    [[nodiscard]] int failedCount() const {
        return summary_.failed;
    }
    [[nodiscard]] double nextNumber() const;
    [[nodiscard]] QString nextTitle() const;
    [[nodiscard]] QUrl nextImage() const;
    [[nodiscard]] double nextProgress() const;
    [[nodiscard]] QString nextState() const;
    [[nodiscard]] bool nextHasFile() const;
    [[nodiscard]] bool nextBusy() const;
    [[nodiscard]] bool nextFailed() const;

    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] QString problem() const {
        return problem_;
    }
    [[nodiscard]] QString downloadFolder() const;
    [[nodiscard]] bool canSendToJDownloader() const;
    [[nodiscard]] EpisodeModel* episodes() {
        return &episodes_;
    }
    [[nodiscard]] CardModel* related() {
        return &related_;
    }

    Q_INVOKABLE void primary();
    Q_INVOKABLE void openNext();
    Q_INVOKABLE void play(double number);
    Q_INVOKABLE void download(double number);
    // Each asks first from 25 episodes on (downloadConfirmationNeeded).
    Q_INVOKABLE void downloadMissing();
    Q_INVOKABLE void downloadUnwatched();
    Q_INVOKABLE void downloadSelection();
    // Without asking: the caller already showed what and how many.
    Q_INVOKABLE void downloadEpisodes(const QList<double>& numbers);
    Q_INVOKABLE void downloadRange(double from, double to);
    Q_INVOKABLE int missingBetween(double from, double to) const;
    Q_INVOKABLE void pauseSeries();
    Q_INVOKABLE void resumeSeries();
    Q_INVOKABLE void retryFailed();
    // Asks first when downloaded bytes would be lost (cancelConfirmationNeeded).
    Q_INVOKABLE void cancelSeries(bool confirmed = false);
    Q_INVOKABLE void pauseDownload(double number);
    Q_INVOKABLE void resumeDownload(double number);
    Q_INVOKABLE void cancelDownload(double number, bool confirmed = false);
    Q_INVOKABLE void retryDownload(double number);
    Q_INVOKABLE void markWatched(double number);
    Q_INVOKABLE void markUnwatched(double number);
    Q_INVOKABLE void markWatchedThrough(double number);
    Q_INVOKABLE void markAllWatched();
    Q_INVOKABLE void markAllUnwatched();
    Q_INVOKABLE void markSelection(bool watched);
    // A position typed by the user ("12:34", "1:02:03"): whether it can be saved, and what is
    // wrong with it once that is certain (an empty text while it may still become valid).
    Q_INVOKABLE bool clockValid(double number, const QString& clock) const;
    Q_INVOKABLE QString clockProblem(double number, const QString& clock) const;
    Q_INVOKABLE void continueFrom(double number, const QString& clock);
    Q_INVOKABLE void trashFile(double number);
    Q_INVOKABLE void trashWatched();
    Q_INVOKABLE void showFile(double number);
    Q_INVOKABLE bool hasFolder() const;
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void openTrailer();
    Q_INVOKABLE void sendToJDownloader(double number);
    Q_INVOKABLE void setInLibrary(bool value);
    Q_INVOKABLE void setFavorite(bool value);
    Q_INVOKABLE void setNotify(bool value);
    Q_INVOKABLE void setAutoDownload(bool value);
    Q_INVOKABLE void reload();
    // Reverts the change last announced with `undoable`.
    Q_INVOKABLE void undo();

signals:
    void slugChanged();
    void changed();
    // A short outcome for the user; `problem` marks failures.
    void message(const QString& text, bool problem);
    // A change that did not ask first; `undo()` reverts it.
    void undoable(const QString& text);
    // `selection`: the numbers come from the selection, which empties once they are queued.
    void downloadConfirmationNeeded(const QList<double>& numbers, const QString& question,
                                    const QString& detail, bool selection);
    // `number` < 0: the whole series.
    void cancelConfirmationNeeded(double number, const QString& question, const QString& detail);

private:
    struct Summary {
        PrimaryAction primary = PrimaryAction::None;
        std::optional<double> target; // the episode the main button acts on
        QList<double> missing;
        QList<double> unwatchedMissing;
        int watched = 0;
        int onDisk = 0;
        qint64 diskBytes = 0;
        bool watchedOnDisk = false;
        bool watching = false;
        bool running = false;
        bool paused = false;
        QDateTime retryAt; // valid when what runs only waits to try again
        int batchDone = 0;
        int batchTotal = 0;
        double batchProgress = 0;
        int failed = 0;
        qint64 partialBytes = 0;
    };
    struct SavedState {
        double number = 0;
        library::WatchState state = library::WatchState::Unwatched;
        qint64 positionMs = 0;
        qint64 durationMs = 0;
    };

    void readLibrary();
    void readTasks();
    void fetchDetail();
    void loadFailed();
    void update();
    void report(const storage::Stored<void>& result);
    void request(const QList<double>& numbers, bool selection = false);
    void joinLibrary();
    [[nodiscard]] const library::AnimeRecord& source() const;
    [[nodiscard]] QString folderPath() const;
    [[nodiscard]] Summary summarize() const;
    void rememberRelated();
    [[nodiscard]] QList<SavedState>
    saveStates(const std::function<bool(const library::EpisodeRecord&)>& chosen) const;
    void offerUndo(const QString& text, QList<SavedState> saved);
    void offerRestore(const QString& text, QList<library::TrashedFile> files);
    void restore(const QString& slug, const QList<SavedState>& saved);

    Services services_;
    QString slug_;
    library::AnimeRecord hint_;
    std::optional<library::LibraryEntry> record_;
    std::optional<api::AnimeDetail> detail_;
    QList<downloads::Task> tasks_; // every task of this anime, finished ones too
    EpisodeModel episodes_;
    CardModel related_;
    Summary summary_;
    QString problem_;
    std::function<void()> undo_;
    QTimer notify_timer_;
    bool positioned_ = false;
    bool fetching_ = false;
    quint64 generation_ = 0;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_ANIME_VIEW_H
