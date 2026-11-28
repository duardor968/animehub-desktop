#ifndef ANIMEHUB_APP_ANIME_VIEW_H
#define ANIMEHUB_APP_ANIME_VIEW_H

#include "app_settings.h"
#include "card_model.h"
#include "downloads/download_manager.h"
#include "episode_model.h"
#include "library/library_service.h"
#include "playback/player.h"
#include "schedule.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>

#include <functional>

namespace animehub::app {

struct Services {
    struct Shell {
        std::function<void(const QString&)> open;
        std::function<void(const QString&)> reveal;
        std::function<void(const QUrl&)> openUrl;
    };

    api::ApiClient& api;
    library::LibraryService& library;
    downloads::DownloadManager& downloads;
    playback::Player& player;
    AppSettings& settings;
    Shell shell{};
};

// One anime's page: its details, the user's flags, the main action and the figures beside it,
// the episode list and related anime. Works the same for library and catalog anime; watching or
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
    // "7,4": Spanish-formatted score alone; empty when missing or <= 0.
    Q_PROPERTY(QString scoreText READ scoreText NOTIFY changed)
    // "1.842 votos" / "1 voto"; empty when votes <= 0.
    Q_PROPERTY(QString votesText READ votesText NOTIFY changed)
    // Explicit attribution for the community score shown on the detail panel.
    Q_PROPERTY(QString scoreSource READ scoreSource CONSTANT)
    // "TV · 2023 · Finalizado · 28 episodios"
    Q_PROPERTY(QString facts READ facts NOTIFY changed)
    // "En emisión", "Finalizado", "Próximamente", "En pausa", or empty if unknown.
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // "airing", "finished", "upcoming", "paused", or empty.
    Q_PROPERTY(QString statusTone READ statusTone NOTIFY changed)
    // Plain metadata without status: "TV · 2026 · 8 episodios" or "Película · 2024".
    Q_PROPERTY(QString metaLine READ metaLine NOTIFY changed)
    Q_PROPERTY(QString genres READ genres NOTIFY changed)
    // Interactive genre chips: [{name, slug}] from API detail, or slugified fallback offline.
    Q_PROPERTY(QVariantList genreChips READ genreChips NOTIFY changed)
    // Link to the anime on AnimeAV1 (empty offline or when unavailable).
    Q_PROPERTY(QString sourceUrl READ sourceUrl NOTIFY changed)
    Q_PROPERTY(bool mature READ mature NOTIFY changed)
    // Semantic status chip only (empty when status is unknown): [{text, tone}].
    Q_PROPERTY(QVariantList chips READ chips NOTIFY changed)
    // Structured metadata rows for the page's side column: [{label, name, value, icon?}], empty
    // omitted.
    Q_PROPERTY(QVariantList details READ details NOTIFY changed)
    Q_PROPERTY(bool hasDetails READ hasDetails NOTIFY changed)
    Q_PROPERTY(QUrl trailerUrl READ trailerUrl NOTIFY changed)
    // A film or any work of one episode: no episode list.
    Q_PROPERTY(bool single READ single NOTIFY changed)
    // Airing or upcoming: new episodes can still arrive.
    Q_PROPERTY(bool airing READ airing NOTIFY changed)
    Q_PROPERTY(bool inLibrary READ inLibrary NOTIFY changed)
    Q_PROPERTY(bool favorite READ favorite NOTIFY changed)
    Q_PROPERTY(bool notify READ notify NOTIFY changed)
    Q_PROPERTY(bool autoDownload READ autoDownload NOTIFY changed)

    // Dual-Role main action: Play/Rewatch when the target episode is on disk (paired with the
    // secondary batch download SplitButton whenever others are missing); otherwise stays on global
    // DownloadAll while unqueued episodes remain, switching to PauseAll/ResumeAll/Failed only once
    // every missing episode is already in the queue. Single-episode works follow their one task.
    Q_PROPERTY(PrimaryAction primaryAction READ primaryAction NOTIFY changed)
    Q_PROPERTY(QString primaryText READ primaryText NOTIFY changed)
    Q_PROPERTY(bool primaryEnabled READ primaryEnabled NOTIFY changed)
    // What a press does to the download, in full: "Pausar la descarga del episodio 9". Empty
    // when the text says it all.
    Q_PROPERTY(QString primaryHint READ primaryHint NOTIFY changed)
    // Share of that episode's file already transferred (Downloading, Paused, PauseAll, ResumeAll).
    Q_PROPERTY(double primaryProgress READ primaryProgress NOTIFY changed)
    // True when primaryAction is Play or Rewatch and unqueued episodes remain to download.
    Q_PROPERTY(bool showSecondaryDownload READ showSecondaryDownload NOTIFY changed)
    // Label for the batch download SplitButton: "Descargar ep. 5", "Descargar restantes (7)", or
    // "Descargar todo (8)".
    Q_PROPERTY(QString batchDownloadText READ batchDownloadText NOTIFY changed)
    // Episodes neither on disk nor on their way; of those, the unwatched ones. As a label
    // names them: "ep. 9", "7 episodios".
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(QString missingText READ missingText NOTIFY changed)
    Q_PROPERTY(int unwatchedMissingCount READ unwatchedMissingCount NOTIFY changed)
    Q_PROPERTY(double firstMissing READ firstMissing NOTIFY changed)
    Q_PROPERTY(double firstNumber READ firstNumber NOTIFY changed)
    Q_PROPERTY(double lastNumber READ lastNumber NOTIFY changed)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY changed) // unfinished ones
    Q_PROPERTY(bool watchedOnDisk READ watchedOnDisk NOTIFY changed)
    Q_PROPERTY(int onDiskCount READ onDiskCount NOTIFY changed)
    // Something is watched or under way.
    Q_PROPERTY(bool started READ started NOTIFY changed)
    Q_PROPERTY(double watchedProgress READ watchedProgress NOTIFY changed)
    // The series' downloads: some transferring or waiting their turn, or all of them paused.
    Q_PROPERTY(bool downloadRunning READ downloadRunning NOTIFY changed)
    Q_PROPERTY(bool downloadPaused READ downloadPaused NOTIFY changed)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY changed)
    // The figures beside the main button, each only when it applies, in the order they give
    // way to a narrow window: [{caption, value, progress (< 0: no bar), problem}].
    Q_PROPERTY(QVariantList figures READ figures NOTIFY changed)

    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString downloadFolder READ downloadFolder NOTIFY changed)
    Q_PROPERTY(EpisodeModel* episodes READ episodes CONSTANT)
    Q_PROPERTY(CardModel* related READ related CONSTANT)

public:
    // What the main button does with the episode to watch next: watch it, download it, or
    // follow its download (queued or waiting to retry, transferring, paused, failed). With
    // everything watched: wait for the next one of an airing series, or watch it again. For the
    // whole series: download what is missing, pause or resume its batch.
    enum class PrimaryAction {
        None,
        Play,
        Download,
        Downloading,
        Paused,
        Queued,
        Failed,
        Upcoming,
        Rewatch,
        DownloadAll,
        PauseAll,
        ResumeAll
    };
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
    [[nodiscard]] QString scoreText() const;
    [[nodiscard]] QString votesText() const;
    [[nodiscard]] QString scoreSource() const;
    [[nodiscard]] QString facts() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString statusTone() const;
    [[nodiscard]] QString metaLine() const;
    [[nodiscard]] QString genres() const;
    [[nodiscard]] QVariantList genreChips() const;
    [[nodiscard]] QString sourceUrl() const;
    [[nodiscard]] bool mature() const;
    [[nodiscard]] QVariantList chips() const;
    [[nodiscard]] QVariantList details() const;
    [[nodiscard]] bool hasDetails() const;
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
    [[nodiscard]] bool primaryEnabled() const;
    [[nodiscard]] QString primaryHint() const;
    [[nodiscard]] double primaryProgress() const;
    [[nodiscard]] bool showSecondaryDownload() const;
    [[nodiscard]] QString batchDownloadText() const;
    [[nodiscard]] int missingCount() const {
        return static_cast<int>(summary_.missing.size());
    }
    [[nodiscard]] QString missingText() const {
        return EpisodeModel::countText(summary_.missing);
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
    [[nodiscard]] int onDiskCount() const {
        return summary_.onDisk;
    }
    [[nodiscard]] bool started() const {
        return summary_.watched > 0 || summary_.watching;
    }
    [[nodiscard]] double watchedProgress() const;
    [[nodiscard]] bool downloadRunning() const {
        return summary_.running;
    }
    [[nodiscard]] bool downloadPaused() const {
        return summary_.paused;
    }
    [[nodiscard]] int failedCount() const {
        return summary_.failed;
    }
    [[nodiscard]] QVariantList figures() const;

    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] QString problem() const {
        return problem_;
    }
    [[nodiscard]] QString downloadFolder() const;
    [[nodiscard]] EpisodeModel* episodes() {
        return &episodes_;
    }
    [[nodiscard]] CardModel* related() {
        return &related_;
    }

    Q_INVOKABLE void primary();
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
    // "Reintentar ahora": the series' waiting downloads try again without waiting for their time.
    Q_INVOKABLE void retryWaiting();
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
    Q_INVOKABLE void openSource();
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
    // Something done here (a download, watching, a mark, the button) brought the anime into the
    // library.
    void addedToLibrary(const QString& slug, const QString& title);
    // `selection`: the numbers come from the selection, which empties once they are queued.
    void downloadConfirmationNeeded(const QList<double>& numbers, const QString& question,
                                    const QString& detail, bool selection);
    // `number` < 0: the whole series.
    void cancelConfirmationNeeded(double number, const QString& question, const QString& detail);

private:
    struct Summary {
        PrimaryAction primary = PrimaryAction::None;
        std::optional<double> target;       // the episode the main button acts on
        std::optional<downloads::Task> job; // its unfinished download
        int place = 0;                      // queued: its place among those waiting for a slot
        QList<double> missing;
        QList<double> unwatchedMissing;
        int watched = 0;
        int onDisk = 0;
        qint64 diskBytes = 0;
        bool watchedOnDisk = false;
        bool watching = false;
        bool running = false;
        bool paused = false;
        bool transferring = false; // a file is found, transferred or checked
        int active = 0;            // unfinished downloads that are not failed
        QDateTime retryAt;         // valid when what runs only waits to try again
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
    void fetchSchedule();
    void loadFailed();
    void update();
    void report(const storage::Stored<void>& result);
    void request(const QList<double>& numbers, bool selection = false);
    [[nodiscard]] const library::AnimeRecord& source() const;
    [[nodiscard]] QString folderPath() const;
    [[nodiscard]] Summary summarize() const;
    // Before the first episode, when it premieres.
    [[nodiscard]] QDateTime premiere() const;
    // The series' downloads, not one episode's: several of them are on their way.
    [[nodiscard]] bool batch() const;
    // Joins the library when it is not there yet, saying so.
    void joinLibrary();
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
    std::optional<schedule::Next> next_airing_; // from the weekly schedule, as on the home
    QList<downloads::Task> tasks_;              // every task of this anime, finished ones too
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
