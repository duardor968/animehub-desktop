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
    QString label; // "Episodio 7"
    QUrl image;    // the episode's still
    QString state; // downloads::stateKey
    double progress = 0;
    QString detail; // display::download
    bool problem = false;
    bool hasFile = false; // completed and still on disk
    QString engine;
    QString source;
    QString connection;
    QString destination;
    QString phase;
    bool canPause = false;
    bool canResume = false;
    bool canRetry = false;
    QString partial; // what cancelling deletes ("143 MB"); empty when nothing

    bool operator==(const DownloadEpisodeRow&) const = default;
};

// A series on the Downloads page: its episodes in the list and what they add up to.
struct DownloadSeriesRow {
    QString slug;
    QString title;
    QUrl art;        // backdrop, else poster
    QString summary; // "3 de 12 · 1,1 GB de 4,2 GB"
    double progress = 0;
    int failed = 0;
    QString failedText; // "1 con error"
    bool canPause = false;
    bool canResume = false;
    bool canRetry = false;
    QString engines;
    QString destinations;
    bool first = false;  // first in line
    QString cancelText;  // what cancelling deletes, said in full; empty when nothing
    int filesOnDisk = 0; // completed episodes whose file is still there
    QString diskText;    // "26 episodios · 9,1 GB"
    QString watchText;   // "Ver ep. 5", "Continuar · Ep. 13"; empty when nothing to watch
    bool hasFolder = false;
    QList<DownloadEpisodeRow> episodes;

    bool operator==(const DownloadSeriesRow&) const = default;
};

// The episodes of one series, kept in step with its row.
class DownloadEpisodesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NumberRole,
        LabelRole,
        ImageRole,
        StateRole,
        ProgressRole,
        DetailRole,
        ProblemRole,
        HasFileRole,
        PartialRole,
        EngineRole,
        SourceRole,
        ConnectionRole,
        DestinationRole,
        PhaseRole,
        CanPauseRole,
        CanResumeRole,
        CanRetryRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setRows(const QList<DownloadEpisodeRow>& rows);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

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
        ArtRole,
        SummaryRole,
        ProgressRole,
        FailedRole,
        FailedTextRole,
        CanPauseRole,
        CanResumeRole,
        FirstRole,
        CanRetryRole,
        EnginesRole,
        DestinationsRole,
        CancelTextRole,
        FilesOnDiskRole,
        DiskTextRole,
        WatchTextRole,
        HasFolderRole,
        EpisodesRole,
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
