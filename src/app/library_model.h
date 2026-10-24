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
// shown when nothing is selected: the next episode to watch in large, the shelves of what is
// ready to watch and what to download, and the grid. The filter applies to the list and the
// grid; the search only to the list.
class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    // Two ways of showing, beside the filter: only what has an episode on disk, and a
    // franchise's anime under it.
    Q_PROPERTY(bool onDiskOnly READ onDiskOnly WRITE setOnDiskOnly NOTIFY filterChanged)
    Q_PROPERTY(bool grouped READ grouped WRITE setGrouped NOTIFY filterChanged)
    // How many anime each filter lets through, by Filter value, with "solo en disco" applied.
    Q_PROPERTY(QList<int> filterCounts READ filterCounts NOTIFY countChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool archivedOnly READ archivedOnly WRITE setArchivedOnly NOTIFY archivedOnlyChanged)
    Q_PROPERTY(int archivedCount READ archivedCount NOTIFY countChanged)
    Q_PROPERTY(QVariantMap franchise READ franchise NOTIFY franchiseChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList overview READ overview NOTIFY countChanged)
    // The anime watched last, shown in large on the home: slug, title, backdropUrl, posterUrl,
    // detail, progress (< 0 hides the bar), action (FeaturedAction) and actionText. Empty when
    // nothing that passes the filter is being watched.
    Q_PROPERTY(QVariantMap featured READ featured NOTIFY featuredChanged)
    // What is followed, split by what it waits for: its next episode is on disk, or episodes
    // still have to be downloaded.
    Q_PROPERTY(CardModel* readyShelf READ readyShelf CONSTANT)
    Q_PROPERTY(CardModel* downloadShelf READ downloadShelf CONSTANT)
    // The episodes "Descargar todo" queues: every missing one of every anime on the shelf.
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countChanged)
    // How many anime each shelf holds, the featured one counted with "Para ver".
    Q_PROPERTY(int readyCount READ readyCount NOTIFY countChanged)
    Q_PROPERTY(int downloadCount READ downloadCount NOTIFY countChanged)
    // What airs next among the anime of the library, by day: [{day, items}].
    Q_PROPERTY(QVariantList upcomingDays READ upcomingDays NOTIFY upcomingChanged)
    Q_PROPERTY(int upcomingCount READ upcomingCount NOTIFY upcomingChanged)
    Q_PROPERTY(CardModel* allShelf READ allShelf CONSTANT)

public:
    // Where the user is with an anime, or whether it airs; one at a time.
    enum class Filter { All, Watching, Airing, NotStarted, Watched, Favorites };
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
        GroupIdRole,
        IsGroupRole,
        DepthRole,
        ExpandedRole,
        // Something unwatched is on disk: the row reads bright, as Steam's installed games.
        ReadyRole,
        // An episode of it is queued or transferring.
        DownloadingRole,
    };

    LibraryModel(library::LibraryService& library, downloads::DownloadManager& downloads,
                 api::ApiClient& api, QObject* parent = nullptr);

    // Starts following the airing schedule for the "Próximos episodios" shelf.
    void start();

    [[nodiscard]] Filter filter() const {
        return filter_;
    }
    void setFilter(Filter filter);
    [[nodiscard]] bool onDiskOnly() const {
        return on_disk_only_;
    }
    void setOnDiskOnly(bool on_disk_only);
    [[nodiscard]] bool grouped() const {
        return grouped_;
    }
    [[nodiscard]] QList<int> filterCounts() const;
    void setGrouped(bool grouped);
    [[nodiscard]] QString search() const {
        return search_;
    }
    void setSearch(const QString& search);
    [[nodiscard]] int total() const {
        return total_;
    }
    bool archivedOnly() const {
        return archived_only_;
    }
    void setArchivedOnly(bool archived);
    int archivedCount() const {
        return archived_count_;
    }
    QVariantMap franchise() const {
        return franchise_;
    }
    QString error() const {
        return error_;
    }
    QVariantList overview() const {
        return overview_;
    }
    Q_INVOKABLE int indexOfGroup(qint64 id) const;
    Q_INVOKABLE void toggleGroup(qint64 id);
    Q_INVOKABLE void openFranchise(qint64 id);
    Q_INVOKABLE qint64 groupAt(int row) const;
    Q_INVOKABLE bool archive(const QStringList& slugs, bool archived);
    Q_INVOKABLE bool removePersonal(const QStringList& slugs);
    Q_INVOKABLE bool setFranchiseMember(qint64 id, const QString& slug, bool included);
    Q_INVOKABLE QVariantList memberChoices(qint64 id) const;
    Q_INVOKABLE bool createFranchise(const QString& title, const QString& slug);
    [[nodiscard]] QVariantMap featured() const {
        return featured_;
    }
    [[nodiscard]] CardModel* readyShelf() {
        return &ready_shelf_;
    }
    [[nodiscard]] CardModel* downloadShelf() {
        return &download_shelf_;
    }
    [[nodiscard]] int pendingCount() const {
        return pending_count_;
    }
    [[nodiscard]] int readyCount() const {
        return ready_count_;
    }
    [[nodiscard]] int downloadCount() const {
        return download_count_;
    }
    // Queues every episode of the "Para descargar" shelf, or those of one anime on it; the
    // error, if one could not be.
    Q_INVOKABLE QString downloadPending();
    Q_INVOKABLE QString downloadPendingOf(const QString& slug);
    [[nodiscard]] QVariantList upcomingDays() const {
        return upcoming_days_;
    }
    [[nodiscard]] int upcomingCount() const {
        return upcoming_count_;
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
    void upcomingChanged();
    void archivedOnlyChanged();
    void franchiseChanged();
    void errorChanged();
    // An anime entered or left the library.
    void membershipChanged();

private:
    void reload();
    void rebuildRows();
    void rebuildShelves();
    void rebuildUpcoming(const QList<const library::LibraryEntry*>& shown);
    void updateFeatured();
    void fetchSchedule();
    void rebuildFranchise();
    QVariantMap workMap(const library::LibraryEntry& entry) const;
    bool report(const library::Stored<void>& result);
    [[nodiscard]] bool passes(const library::LibraryEntry& entry) const;
    [[nodiscard]] const library::LibraryEntry* entryFor(const QString& slug) const;
    [[nodiscard]] static QString captionOf(const library::LibraryEntry& entry);
    [[nodiscard]] static QString readyCaption(const library::LibraryEntry& entry);
    [[nodiscard]] static QString pendingCaption(const QList<double>& pending);
    [[nodiscard]] static double progressOf(const library::LibraryEntry& entry);
    [[nodiscard]] static bool started(const library::LibraryEntry& entry);
    [[nodiscard]] static bool following(const library::LibraryEntry& entry);
    [[nodiscard]] static bool ready(const library::LibraryEntry& entry);
    [[nodiscard]] QList<double> pendingOf(const library::LibraryEntry& entry) const;
    [[nodiscard]] bool unfolded(qint64 id) const;
    [[nodiscard]] static QString sortKey(const QString& title);
    [[nodiscard]] static QString distinctTitle(const QString& title, const QString& franchise);

    library::LibraryService& library_;
    downloads::DownloadManager& downloads_;
    api::ApiClient& api_;
    QList<library::LibraryEntry> entries_;
    QSet<QString> slugs_;
    struct Row {
        int entry = -1;
        qint64 group = 0;
        int depth = 0;
        bool operator==(const Row&) const = default;
    };
    QList<Row> rows_;
    QList<library::FranchiseRecord> franchises_;
    QSet<qint64> expanded_;
    QSet<QString> downloading_; // anime with an episode queued or transferring
    bool archived_only_ = false;
    int total_ = 0;
    int archived_count_ = 0;
    qint64 selected_franchise_ = 0;
    QVariantMap franchise_;
    QVariantList overview_;
    QString error_;
    Filter filter_ = Filter::All;
    bool on_disk_only_ = false;
    bool grouped_ = true;
    QString search_;
    QString featured_slug_;
    bool featured_on_disk_ = false;
    QVariantMap featured_;
    CardModel ready_shelf_;
    CardModel download_shelf_;
    int pending_count_ = 0;
    int ready_count_ = 0;
    int download_count_ = 0;
    QVariantList upcoming_days_;
    int upcoming_count_ = 0;
    CardModel all_shelf_;
    QList<api::ScheduleEntry> schedule_;
    bool schedule_stale_ = false; // the last answer came from the cache
    QTimer reload_timer_;
    QTimer rebuild_timer_; // a download came or went: what is missing changed
    QTimer schedule_timer_;
    QTimer clock_timer_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_LIBRARY_MODEL_H
