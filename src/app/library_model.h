#ifndef ANIMEHUB_APP_LIBRARY_MODEL_H
#define ANIMEHUB_APP_LIBRARY_MODEL_H

#include "api/api_client.h"
#include "card_model.h"
#include "downloads/download_manager.h"
#include "library/library_service.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>

namespace animehub::app {

// The library list on the left of the Library page, with its filter and search, plus the home
// shown when nothing is selected: the shelves of what is ready to watch and what to download,
// and the grid. The filter applies to the list and the grid; a search looks through the whole
// library (the Archivo apart) whatever the filter.
class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    // Two ways of showing, beside the filter: only what has an episode on disk, and a
    // franchise's anime under it.
    Q_PROPERTY(bool onDiskOnly READ onDiskOnly WRITE setOnDiskOnly NOTIFY filterChanged)
    Q_PROPERTY(bool grouped READ grouped WRITE setGrouped NOTIFY filterChanged)
    Q_PROPERTY(bool hasGroups READ hasGroups NOTIFY groupsChanged)
    Q_PROPERTY(bool allExpanded READ allExpanded NOTIFY groupsChanged)
    // How many anime each filter lets through, by Filter value, with "solo en disco" applied.
    Q_PROPERTY(QList<int> filterCounts READ filterCounts NOTIFY countChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // The works the list shows, a franchise counted by its works that pass.
    Q_PROPERTY(int shownCount READ shownCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool archivedOnly READ archivedOnly WRITE setArchivedOnly NOTIFY archivedOnlyChanged)
    Q_PROPERTY(int archivedCount READ archivedCount NOTIFY countChanged)
    Q_PROPERTY(QVariantMap franchise READ franchise NOTIFY franchiseChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList overview READ overview NOTIFY countChanged)
    // The first of "Para ver", displayed in the full-width home Hero: slug, title, backdropUrl,
    // posterUrl, detail, progress (< 0 hides the bar) and actionText. Empty when nothing is ready
    // to watch.
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
    // Today's episodes among followed, non-archived library anime (sorted chronologically).
    Q_PROPERTY(QVariantList upcoming READ upcoming NOTIFY scheduleChanged)
    // Compatibility wrapper for the today rail: [{day: "Hoy", isToday: true, items}] or empty.
    Q_PROPERTY(QVariantList upcomingDays READ upcomingDays NOTIFY upcomingChanged)
    Q_PROPERTY(int upcomingCount READ upcomingCount NOTIFY scheduleChanged)
    Q_PROPERTY(QString todayHeading READ todayHeading NOTIFY scheduleChanged)
    // Full catalog Monday–Sunday weekly schedule, sorted by (weekday, slotMinutes, title).
    Q_PROPERTY(QVariantList weeklySchedule READ weeklySchedule NOTIFY scheduleChanged)
    Q_PROPERTY(int todayWeekday READ todayWeekday NOTIFY scheduleChanged)
    Q_PROPERTY(bool scheduleStale READ scheduleStale NOTIFY scheduleChanged)
    Q_PROPERTY(bool scheduleLoaded READ scheduleLoaded NOTIFY scheduleChanged)
    Q_PROPERTY(QString localTimeText READ localTimeText NOTIFY clockChanged)
    // Whether the window is on screen; pauses the clock timer while hidden in tray.
    Q_PROPERTY(bool windowShown READ windowShown WRITE setWindowShown NOTIFY windowShownChanged)

public:
    // Where the user is with an anime, or whether it airs; one at a time. ToWatch is the home's
    // "Para ver": something unwatched is on disk. New values go last: the choice is saved.
    enum class Filter { All, Watching, Airing, NotStarted, Watched, Favorites, ToWatch };
    Q_ENUM(Filter)

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
    };

    LibraryModel(library::LibraryService& library, downloads::DownloadManager& downloads,
                 api::ApiClient& api, QObject* parent = nullptr);

    // Starts following the airing schedule for the "Esta semana" agenda.
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
    [[nodiscard]] bool hasGroups() const {
        return has_groups_;
    }
    [[nodiscard]] bool allExpanded() const {
        return all_expanded_;
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
    [[nodiscard]] int shownCount() const {
        return shown_count_;
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
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();
    Q_INVOKABLE void openFranchise(qint64 id);
    Q_INVOKABLE qint64 groupAt(int row) const;
    Q_INVOKABLE bool archive(const QStringList& slugs, bool archived);
    Q_INVOKABLE bool removePersonal(const QStringList& slugs);
    Q_INVOKABLE bool setFranchiseMember(qint64 id, const QString& slug, bool included);
    Q_INVOKABLE QVariantList memberChoices(qint64 id) const;
    Q_INVOKABLE bool createFranchise(const QString& title, const QString& slug);
    [[nodiscard]] CardModel* readyShelf() {
        return &ready_shelf_;
    }
    [[nodiscard]] CardModel* downloadShelf() {
        return &download_shelf_;
    }
    [[nodiscard]] QVariantMap featured() const {
        return featured_;
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
    [[nodiscard]] QVariantList upcoming() const {
        return upcoming_;
    }
    [[nodiscard]] QVariantList upcomingDays() const {
        return upcoming_days_;
    }
    [[nodiscard]] int upcomingCount() const {
        return upcoming_count_;
    }
    [[nodiscard]] QString todayHeading() const {
        return today_heading_;
    }
    [[nodiscard]] QVariantList weeklySchedule() const {
        return weekly_schedule_;
    }
    [[nodiscard]] int todayWeekday() const {
        return today_weekday_;
    }
    [[nodiscard]] bool scheduleStale() const {
        return schedule_stale_;
    }
    [[nodiscard]] bool scheduleLoaded() const {
        return schedule_loaded_;
    }
    [[nodiscard]] QString localTimeText() const {
        return local_time_text_;
    }
    Q_INVOKABLE void refreshSchedule();
    [[nodiscard]] bool windowShown() const {
        return window_shown_;
    }
    void setWindowShown(bool shown);
    [[nodiscard]] bool clockActive() const {
        return clock_timer_.isActive();
    }

    Q_INVOKABLE int indexOf(const QString& slug) const;
    Q_INVOKABLE QString slugAt(int row) const;
    Q_INVOKABLE bool contains(const QString& slug) const;
    // The breadcrumb's step between the library and an anime: its franchise, {id, title}. Empty
    // when it has none, when the franchise holds only this series or carries its name.
    Q_INVOKABLE QVariantMap franchiseStep(const QString& slug) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void filterChanged();
    void groupsChanged();
    void searchChanged();
    void countChanged();
    void featuredChanged();
    void upcomingChanged();
    void scheduleChanged();
    void clockChanged();
    void windowShownChanged();
    void archivedOnlyChanged();
    void franchiseChanged();
    void errorChanged();
    // An anime entered or left the library.
    void membershipChanged();

private:
    void reload();
    void rebuildRows(bool force_groups_emit = false);
    void rebuildShelves();
    void tickClock();
    void updateClock(const QDateTime& now, bool notify);
    void rebuildSchedule(bool force_emit = false);
    void updateFeatured();
    void fetchSchedule();
    void rebuildFranchise();
    QVariantMap workMap(const library::LibraryEntry& entry) const;
    bool report(const library::Stored<void>& result);
    [[nodiscard]] bool filterActive() const {
        return !search_.isEmpty() || filter_ != Filter::All || on_disk_only_;
    }
    [[nodiscard]] bool passes(const library::LibraryEntry& entry) const;
    // On the list now: in the place shown (library or Archivo) and, with a search, named by it
    // (or its franchise is), else let through by the filter.
    [[nodiscard]] bool shown(const library::LibraryEntry& entry, bool franchise_named) const;
    [[nodiscard]] bool named(const QString& title) const;
    [[nodiscard]] const library::LibraryEntry* entryFor(const QString& slug) const;
    [[nodiscard]] const library::FranchiseRecord* franchiseFor(qint64 id) const;
    [[nodiscard]] static QString captionOf(const library::LibraryEntry& entry);
    [[nodiscard]] static QString readyCaption(const library::LibraryEntry& entry);
    [[nodiscard]] static QString pendingCaption(const library::LibraryEntry& entry,
                                                const QList<double>& pending);
    [[nodiscard]] static QString seenCaption(const library::LibraryEntry& entry);
    [[nodiscard]] static double progressOf(const library::LibraryEntry& entry);
    [[nodiscard]] static bool started(const library::LibraryEntry& entry);
    [[nodiscard]] static bool following(const library::LibraryEntry& entry);
    [[nodiscard]] static bool toWatch(const library::LibraryEntry& entry);
    [[nodiscard]] QList<double> pendingOf(const library::LibraryEntry& entry) const;
    [[nodiscard]] bool unfolded(qint64 id) const;
    [[nodiscard]] static QString sortKey(const QString& title);
    [[nodiscard]] static QString distinctTitle(const QString& title, const QString& franchise);

    library::LibraryService& library_;
    downloads::DownloadManager& downloads_;
    api::ApiClient& api_;
    QList<library::LibraryEntry> entries_;
    QHash<QString, int> entry_index_;
    struct Row {
        int entry = -1;
        qint64 group = 0;
        int depth = 0;
        bool operator==(const Row&) const = default;
    };
    QList<Row> rows_;
    QList<library::FranchiseRecord> franchises_;
    QHash<qint64, int> franchise_index_;
    QHash<QString, int> member_franchise_index_;
    QSet<qint64> expanded_;
    QSet<qint64> collapsed_filtered_;
    bool has_groups_ = false;
    bool all_expanded_ = false;
    bool archived_only_ = false;
    int total_ = 0;
    int shown_count_ = 0;
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
    QVariantMap featured_;
    CardModel ready_shelf_;
    CardModel download_shelf_;
    int pending_count_ = 0;
    int ready_count_ = 0;
    int download_count_ = 0;
    QVariantList upcoming_;
    QVariantList upcoming_days_;
    int upcoming_count_ = 0;
    QString today_heading_;
    QVariantList weekly_schedule_;
    int today_weekday_ = 0;
    QList<api::ScheduleEntry> schedule_;
    bool schedule_stale_ = false; // the last answer came from cache or server marked it stale
    bool schedule_loaded_ = false;
    QString local_time_text_;
    QElapsedTimer schedule_fetch_elapsed_;
    bool window_shown_ = true;
    bool started_ = false;
    QTimer reload_timer_;
    QTimer rebuild_timer_; // a download came or went: what is missing changed
    QTimer franchise_timer_;
    QTimer schedule_timer_;
    QTimer clock_timer_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_LIBRARY_MODEL_H
