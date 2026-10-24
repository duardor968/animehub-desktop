#ifndef ANIMEHUB_APP_EPISODE_MODEL_H
#define ANIMEHUB_APP_EPISODE_MODEL_H

#include "downloads/download_types.h"
#include "library/library_types.h"

#include <QAbstractListModel>
#include <QSet>
#include <QtQmlIntegration>

namespace animehub::app {

// The episodes of one anime with their watch state and, when there is one, their download. The
// rows are one page of fifty; the selection is kept across pages.
class EpisodeModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    Q_PROPERTY(QStringList pageLabels READ pageLabels NOTIFY pageChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    // Of the selected episodes, those that can be downloaded; as text, "ep. 9" or "7 episodios".
    Q_PROPERTY(int selectedMissing READ selectedMissing NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedMissingText READ selectedMissingText NOTIFY selectionChanged)
    // Qt::CheckState of the page: none, some or all of its rows selected.
    Q_PROPERTY(int pageSelection READ pageSelection NOTIFY selectionChanged)

public:
    static constexpr int kPageSize = 50;

    enum Role {
        NumberRole = Qt::UserRole + 1,
        LabelRole,
        TitleRole,
        DateRole,
        ImageRole,
        WatchStateRole,
        ProgressRole,
        ProgressTextRole,
        FreshRole,
        HasFileRole,
        DownloadStateRole,
        DownloadProgressRole,
        DownloadTextRole,
        SelectedRole,
        FirstRole,
    };

    using QAbstractListModel::QAbstractListModel;

    // How many episodes an action takes, as its label names them: "ep. 9", "7 episodios".
    [[nodiscard]] static QString countText(const QList<double>& numbers);

    // A new anime: its list, on the page that holds `shown`, with nothing selected.
    void load(QList<library::EpisodeRecord> episodes, std::optional<double> shown);
    // The same anime again: rows keep their place.
    void setEpisodes(QList<library::EpisodeRecord> episodes);
    void setTask(const downloads::Task& task);
    void setTasks(const QList<downloads::Task>& tasks);

    [[nodiscard]] const QList<library::EpisodeRecord>& episodes() const {
        return episodes_;
    }
    [[nodiscard]] std::optional<library::EpisodeRecord> episode(double number) const;
    [[nodiscard]] std::optional<downloads::Task> task(double number) const;
    // Neither on disk nor on its way: no task, or one that stopped (paused, failed).
    [[nodiscard]] bool missing(const library::EpisodeRecord& episode) const;
    [[nodiscard]] int rowOf(double number) const;

    [[nodiscard]] int total() const {
        return static_cast<int>(episodes_.size());
    }
    [[nodiscard]] int page() const {
        return page_;
    }
    void setPage(int page);
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] QStringList pageLabels() const;

    [[nodiscard]] int selectedCount() const {
        return static_cast<int>(selected_.size());
    }
    [[nodiscard]] int selectedMissing() const;
    [[nodiscard]] QString selectedMissingText() const;
    [[nodiscard]] int pageSelection() const;
    // The selected episodes in list order.
    [[nodiscard]] QList<double> selection() const;
    // Flips one episode; with `extend`, selects every episode from the last one flipped.
    Q_INVOKABLE void toggle(double number, bool extend);
    Q_INVOKABLE void selectPage(bool selected);
    Q_INVOKABLE void clearSelection();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void pageChanged();
    void selectionChanged();

private:
    [[nodiscard]] int first() const {
        return page_ * kPageSize;
    }
    [[nodiscard]] static int rowsOn(qsizetype size, int page);
    [[nodiscard]] int indexOf(double number) const;
    void refresh(double number);
    void refreshPage();

    QList<library::EpisodeRecord> episodes_;
    QHash<double, downloads::Task> tasks_;
    QSet<double> selected_;
    std::optional<double> anchor_;
    int page_ = 0;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_EPISODE_MODEL_H
