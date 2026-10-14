#ifndef ANIMEHUB_APP_LIBRARY_MODEL_H
#define ANIMEHUB_APP_LIBRARY_MODEL_H

#include "card_model.h"
#include "library/library_service.h"

#include <QAbstractListModel>
#include <QTimer>
#include <QtQmlIntegration>

namespace animehub::app {

// The library list on the left of the Library page, with its filter and search, plus the
// shelves of the library home shown when nothing is selected.
class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(CardModel* continueShelf READ continueShelf CONSTANT)
    Q_PROPERTY(CardModel* freshShelf READ freshShelf CONSTANT)
    Q_PROPERTY(CardModel* allShelf READ allShelf CONSTANT)

public:
    enum class Filter { All, Watching, NotStarted, Watched, Favorites, Downloaded };
    Q_ENUM(Filter)

    enum Role {
        SlugRole = Qt::UserRole + 1,
        TitleRole,
        PosterRole,
        CaptionRole,
        ProgressRole,
        FreshRole,
        FavoriteRole,
    };

    explicit LibraryModel(library::LibraryService& library, QObject* parent = nullptr);

    [[nodiscard]] Filter filter() const {
        return filter_;
    }
    void setFilter(Filter filter);
    [[nodiscard]] QString search() const {
        return search_;
    }
    void setSearch(const QString& search);
    [[nodiscard]] int total() const {
        return static_cast<int>(entries_.size());
    }
    [[nodiscard]] CardModel* continueShelf() {
        return &continue_shelf_;
    }
    [[nodiscard]] CardModel* freshShelf() {
        return &fresh_shelf_;
    }
    [[nodiscard]] CardModel* allShelf() {
        return &all_shelf_;
    }

    Q_INVOKABLE int indexOf(const QString& slug) const;
    Q_INVOKABLE QString slugAt(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void filterChanged();
    void searchChanged();
    void countChanged();

private:
    void reload();
    void rebuildRows();
    [[nodiscard]] static QString captionOf(const library::LibraryEntry& entry);
    [[nodiscard]] static double progressOf(const library::LibraryEntry& entry);
    [[nodiscard]] static bool continuing(const library::LibraryEntry& entry);
    [[nodiscard]] static bool started(const library::LibraryEntry& entry);

    library::LibraryService& library_;
    QList<library::LibraryEntry> entries_;
    QList<int> rows_; // indices into entries_, in display order
    Filter filter_ = Filter::All;
    QString search_;
    CardModel continue_shelf_;
    CardModel fresh_shelf_;
    CardModel all_shelf_;
    QTimer reload_timer_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_LIBRARY_MODEL_H
