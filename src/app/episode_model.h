#ifndef ANIMEHUB_APP_EPISODE_MODEL_H
#define ANIMEHUB_APP_EPISODE_MODEL_H

#include "downloads/download_types.h"
#include "library/library_types.h"

#include <QAbstractListModel>
#include <QtQmlIntegration>

namespace animehub::app {

// The episodes of one anime with their watch state and, when there is one, their download.
class EpisodeModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NumberRole = Qt::UserRole + 1,
        LabelRole,
        TitleRole,
        ImageRole,
        WatchStateRole,
        ProgressRole,
        ProgressTextRole,
        FreshRole,
        HasFileRole,
        DownloadStateRole,
        DownloadProgressRole,
        DownloadTextRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setEpisodes(QList<library::EpisodeRecord> episodes);
    void setTask(const downloads::Task& task);
    void clearTask(double number);
    void setTasks(const QList<downloads::Task>& tasks);

    [[nodiscard]] const QList<library::EpisodeRecord>& episodes() const {
        return episodes_;
    }
    [[nodiscard]] std::optional<library::EpisodeRecord> episode(double number) const;
    [[nodiscard]] std::optional<downloads::Task> task(double number) const;
    Q_INVOKABLE int rowOf(double number) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<library::EpisodeRecord> episodes_;
    QHash<double, downloads::Task> tasks_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_EPISODE_MODEL_H
