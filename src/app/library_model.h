#ifndef ANIMEHUB_APP_LIBRARY_MODEL_H
#define ANIMEHUB_APP_LIBRARY_MODEL_H

#include "api/api_client.h"
#include "card_model.h"
#include "downloads/download_manager.h"
#include "library/library_service.h"

#include <QAbstractListModel>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>

namespace animehub::app {

// The library list on the left of the Library page, with its filter and search, plus the home
// shown when nothing is selected: the anime being watched in large, its shelves and the grid.
// The filter applies to both; the search only to the list.
class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    // The anime watched last, shown in large on the home: slug, title, backdropUrl, posterUrl,
    // detail, progress (< 0 hides the bar), action (FeaturedAction) and actionText. Empty when
    // nothing that passes the filter is being watched.
    Q_PROPERTY(QVariantMap featured READ featured NOTIFY featuredChanged)
    Q_PROPERTY(CardModel* continueShelf READ continueShelf CONSTANT)
    Q_PROPERTY(CardModel* freshShelf READ freshShelf CONSTANT)
    Q_PROPERTY(CardModel* upcomingShelf READ upcomingShelf CONSTANT)
    Q_PROPERTY(CardModel* allShelf READ allShelf CONSTANT)

public:
    enum class Filter { All, Watching, NotStarted, Watched, Favorites, Downloaded };
    Q_ENUM(Filter)
    // What the featured anime's button does with the episode it resumes.
    enum class FeaturedAction { Play, Download, Downloading };
    Q_ENUM(FeaturedAction)

    enum Role {
        SlugRole = Qt::UserRole + 1,
        TitleRole,
        PosterRole,
        CaptionRole,
        ProgressRole,
        FreshRole,
        FavoriteRole,
    };

    LibraryModel(library::LibraryService& library, downloads::DownloadManager& downloads,
                 api::ApiClient& api, QObject* parent = nullptr);

    // Starts following the airing schedule for the "Próximos episodios" shelf.
    void start();

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
    [[nodiscard]] QVariantMap featured() const {
        return featured_;
    }
    [[nodiscard]] CardModel* continueShelf() {
        return &continue_shelf_;
    }
    [[nodiscard]] CardModel* freshShelf() {
        return &fresh_shelf_;
    }
    [[nodiscard]] CardModel* upcomingShelf() {
        return &upcoming_shelf_;
    }
    [[nodiscard]] CardModel* allShelf() {
        return &all_shelf_;
    }

    Q_INVOKABLE int indexOf(const QString& slug) const;
    Q_INVOKABLE QString slugAt(int row) const;
    Q_INVOKABLE bool contains(const QString& slug) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void filterChanged();
    void searchChanged();
    void countChanged();
    void featuredChanged();
    // An anime entered or left the library.
    void membershipChanged();

private:
    void reload();
    void rebuildRows();
    void rebuildShelves();
    void rebuildUpcoming(const QList<const library::LibraryEntry*>& shown);
    void updateFeatured();
    void fetchSchedule();
    [[nodiscard]] bool passes(const library::LibraryEntry& entry) const;
    [[nodiscard]] const library::LibraryEntry* entryFor(const QString& slug) const;
    [[nodiscard]] static QString captionOf(const library::LibraryEntry& entry);
    [[nodiscard]] static double progressOf(const library::LibraryEntry& entry);
    [[nodiscard]] static bool continuing(const library::LibraryEntry& entry);
    [[nodiscard]] static bool started(const library::LibraryEntry& entry);

    library::LibraryService& library_;
    downloads::DownloadManager& downloads_;
    api::ApiClient& api_;
    QList<library::LibraryEntry> entries_;
    QSet<QString> slugs_;
    QList<int> rows_; // indices into entries_, in display order
    Filter filter_ = Filter::All;
    QString search_;
    QString featured_slug_;
    bool featured_on_disk_ = false;
    QVariantMap featured_;
    CardModel continue_shelf_;
    CardModel fresh_shelf_;
    CardModel upcoming_shelf_;
    CardModel all_shelf_;
    QList<api::ScheduleEntry> schedule_;
    bool schedule_stale_ = false; // the last answer came from the cache
    QTimer reload_timer_;
    QTimer schedule_timer_;
    QTimer clock_timer_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_LIBRARY_MODEL_H
