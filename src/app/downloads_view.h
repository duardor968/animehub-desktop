#ifndef ANIMEHUB_APP_DOWNLOADS_VIEW_H
#define ANIMEHUB_APP_DOWNLOADS_VIEW_H

#include "downloads/download_manager.h"
#include "library/library_repository.h"
#include "playback/player.h"

#include <QAbstractListModel>
#include <QtQmlIntegration>

namespace animehub::app {

// The Downloads page and the bar at the bottom of the window.
class DownloadsView final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(int count READ rowCount NOTIFY summaryChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY summaryChanged)
    Q_PROPERTY(bool anyPaused READ anyPaused NOTIFY summaryChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY summaryChanged)
    Q_PROPERTY(QString currentDetail READ currentDetail NOTIFY summaryChanged)
    Q_PROPERTY(double currentProgress READ currentProgress NOTIFY summaryChanged)
    Q_PROPERTY(QString queueText READ queueText NOTIFY summaryChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        SlugRole,
        TitleRole,
        EpisodeRole,
        EpisodeLabelRole,
        PosterRole,
        StateRole,
        SectionRole,
        ProgressRole,
        DetailRole,
        ProblemRole,
    };

    DownloadsView(downloads::DownloadManager& manager, library::LibraryRepository& library,
                  playback::Player& player, QObject* parent = nullptr);

    [[nodiscard]] int activeCount() const;
    [[nodiscard]] int finishedCount() const;
    [[nodiscard]] bool anyPaused() const;
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] QString currentDetail() const;
    [[nodiscard]] double currentProgress() const;
    [[nodiscard]] QString queueText() const;

    Q_INVOKABLE void pause(qint64 id);
    Q_INVOKABLE void resume(qint64 id);
    Q_INVOKABLE void cancel(qint64 id);
    Q_INVOKABLE void retry(qint64 id);
    Q_INVOKABLE void forget(qint64 id);
    Q_INVOKABLE void play(qint64 id);
    Q_INVOKABLE void showFile(qint64 id);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void summaryChanged();
    void message(const QString& text, bool problem);

private:
    void reload();
    void update(qint64 id);
    [[nodiscard]] const downloads::Task* current() const;

    downloads::DownloadManager& manager_;
    library::LibraryRepository& library_;
    playback::Player& player_;
    QList<downloads::Task> tasks_;
    QHash<QString, QUrl> posters_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_DOWNLOADS_VIEW_H
