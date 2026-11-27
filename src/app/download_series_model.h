#ifndef ANIMEHUB_APP_DOWNLOAD_SERIES_MODEL_H
#define ANIMEHUB_APP_DOWNLOAD_SERIES_MODEL_H

#include <QAbstractListModel>
#include <QUrl>
#include <QtQmlIntegration>

namespace animehub::app {

// An episode of a series on the Downloads page, worded for the screen.
struct DownloadEpisodeRow {
    qint64 id = 0;
    double number = 0;
    QString episode;    // "7", "12,5"
    QString label;      // "Episodio 7"
    QString title;      // episode title from library, if known
    QUrl image;         // the episode's still (also exposed as stillUrl)
    QString state;      // downloads::stateKey
    QString stateGroup; // "completed", "running", "queued", "paused", "failed"
    QString stateText;  // "Descargando", "Completado", "En cola", "En pausa", "Error"
    double progress = 0;
    QString percentText; // "64 %", "100 %", "En cola", "En pausa", "Error", "—"
    QString speedText;   // "6,8 MB/s" or "—"
    QString sizeText;    // "288 MB / 450 MB" or "442 MB · En disco" or "—"
    QString etaText;     // "24 s", "4 min"; empty when unknown
    QString provider;    // "MEGA", "Pixeldrain", "JDownloader"; empty when unknown
    QString filePath;    // path on disk when completed and present; empty otherwise
    QString detail;      // display::download
    bool problem = false;
    bool hasFile = false; // completed and still on disk (also exposed as canPlay)
    bool canPause = false;
    bool canResume = false;
    bool canRetry = false;
    bool canRetryNow = false; // waits to try again by itself: "Reintentar ahora"
    bool canCancel = false;
    QString partial; // what cancelling deletes ("143 MB"); empty when nothing

    bool operator==(const DownloadEpisodeRow&) const = default;
};

// A series on the Downloads page: its episodes in the list and what they add up to.
struct DownloadSeriesRow {
    QString slug;
    QString title;
    QUrl poster;
    QString backdrop; // real backdrop URL when different from poster; empty otherwise
    // In line, "3 de 12 · 1,1 GB de 4,2 GB"; completed, "3 episodios · 286 MB · hoy 13:04".
    QString summary;
    double progress = 0;
    int failed = 0;
    QString failedText; // "1 con error"
    bool canPause = false;
    bool canResume = false;
    bool canRetry = false;
    bool canRetryNow = false; // some episode waits to try again by itself
    QString engineLine;       // as its episodes', when JDownloader downloads some of them
    bool first = false;       // first in line
    QString cancelText;       // what cancelling deletes, said in full; empty when nothing
    int filesOnDisk = 0;      // completed episodes whose file is still there
    QString diskText;         // "26 episodios · 9,1 GB"
    QString watchText;        // "Ver ep. 5", "Continuar · Ep. 13"; empty when nothing to watch
    bool hasFolder = false;
    // Why it does not go as it should, beyond failures: "MEGA llegó a su límite · se libera a las
    // 15:20 · sigue con Pixeldrain". Empty when nothing to say.
    QString notice;
    bool running = false;     // an episode looks for its file, transfers or is checked
    int activeCount = 0;      // episodes actively running in this series
    QString runningSpeedText; // combined live speed of this series; empty when 0
    int cancelCount = 0;      // episodes cancelling stops
    bool filesGone = false;   // completed, and none of its files is on disk anymore
    int goneCount = 0;        // completed episodes whose file is gone
    QList<DownloadEpisodeRow> episodes;

    bool operator==(const DownloadSeriesRow&) const = default;
};

// The episodes of one series, kept in step with its row.
class DownloadEpisodesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NumberRole,
        EpisodeRole,
        LabelRole,
        TitleRole,
        ImageRole,
        StillUrlRole,
        StateRole,
        StateGroupRole,
        StateTextRole,
        ProgressRole,
        PercentTextRole,
        SpeedTextRole,
        SizeTextRole,
        EtaTextRole,
        ProviderRole,
        FilePathRole,
        DetailRole,
        ProblemRole,
        HasFileRole,
        CanPlayRole,
        PartialRole,
        CanPauseRole,
        CanResumeRole,
        CanRetryRole,
        CanRetryNowRole,
        CanCancelRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setRows(const QList<DownloadEpisodeRow>& rows);
    [[nodiscard]] const QList<DownloadEpisodeRow>& rows() const {
        return rows_;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<DownloadEpisodeRow> rows_;
};

// The series of one section of the Downloads page. Rows keep their identity (the slug) across
// updates, so the page keeps its delegates, their state and their animations.
class DownloadSeriesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        SlugRole = Qt::UserRole + 1,
        TitleRole,
        PosterRole,
        BackdropRole,
        SummaryRole,
        ProgressRole,
        FailedRole,
        FailedTextRole,
        CanPauseRole,
        CanResumeRole,
        FirstRole,
        CanRetryRole,
        EngineLineRole,
        CancelTextRole,
        FilesOnDiskRole,
        DiskTextRole,
        WatchTextRole,
        HasFolderRole,
        NoticeRole,
        RunningRole,
        ActiveCountRole,
        RunningSpeedTextRole,
        CancelCountRole,
        FilesGoneRole,
        CanRetryNowRole,
        EpisodesRole,
        TasksRole,
        TaskCountRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setRows(QList<DownloadSeriesRow> rows);
    [[nodiscard]] const QList<DownloadSeriesRow>& rows() const {
        return rows_;
    }
    [[nodiscard]] const DownloadSeriesRow* row(const QString& slug) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    DownloadEpisodesModel* episodesOf(const QString& slug);

    QList<DownloadSeriesRow> rows_;
    // Children of this model; one goes with its row, once QML has let go of it.
    QHash<QString, DownloadEpisodesModel*> episodes_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_DOWNLOAD_SERIES_MODEL_H
