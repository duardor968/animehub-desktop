#ifndef ANIMEHUB_APP_CATALOG_VIEW_H
#define ANIMEHUB_APP_CATALOG_VIEW_H

#include "api/api_client.h"
#include "card_model.h"
#include "library/library_types.h"
#include "library_model.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration>

namespace animehub::app {

// The catalog's cards also say whether the anime is in the library, for the mark on the poster.
class CatalogCards final : public CardModel {
public:
    enum Role {
        OwnedRole = CardModel::ActionRole + 1,
    };

    explicit CatalogCards(const LibraryModel& library, QObject* parent = nullptr)
        : CardModel(parent), library_(library) {}

    // The library gained or lost an anime: every mark is looked at again.
    void refreshOwned();

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    const LibraryModel& library_;
};

// The Catalog page: the episodes AnimeHub added last, and search, filters and an order over
// the whole catalog.
class CatalogView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY queryChanged)
    Q_PROPERTY(int status READ status WRITE setStatus NOTIFY queryChanged)
    Q_PROPERTY(QString genre READ genre WRITE setGenre NOTIFY queryChanged)
    // Only the whole catalog follows it; the recent episodes keep theirs.
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY queryChanged)
    // A search or a filter narrows the catalog: the recent episodes give way to the results.
    Q_PROPERTY(bool browsing READ browsing NOTIFY queryChanged)
    Q_PROPERTY(QVariantList genres READ genres NOTIFY genresChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    // A new search or filter is on its way, or failed: the results on view are the previous ones.
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    // The source stops counting at 1000 matches: `total` is then a lower bound.
    Q_PROPERTY(bool capped READ capped NOTIFY stateChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged)
    // The problem came with a later page: it belongs where the list stops, not above it.
    Q_PROPERTY(bool problemBelow READ problemBelow NOTIFY stateChanged)
    // The recent episodes have not arrived yet.
    Q_PROPERTY(bool homeLoading READ homeLoading NOTIFY stateChanged)
    Q_PROPERTY(CardModel* results READ results CONSTANT)
    Q_PROPERTY(CardModel* recentEpisodes READ recentEpisodes CONSTANT)

public:
    // `library` marks the cards of anime already in it.
    CatalogView(api::ApiClient& api, const LibraryModel& library, QObject* parent = nullptr);

    [[nodiscard]] QString search() const {
        return query_.search;
    }
    void setSearch(const QString& search);
    [[nodiscard]] int status() const;
    void setStatus(int status);
    [[nodiscard]] QString genre() const;
    void setGenre(const QString& genre);
    [[nodiscard]] int order() const {
        return static_cast<int>(query_.order);
    }
    void setOrder(int order);
    [[nodiscard]] bool browsing() const;
    [[nodiscard]] QVariantList genres() const {
        return genres_;
    }
    [[nodiscard]] bool loading() const {
        return loading_;
    }
    [[nodiscard]] bool refreshing() const {
        return refreshing_;
    }
    [[nodiscard]] bool hasMore() const {
        return page_ < total_pages_;
    }
    [[nodiscard]] int total() const {
        return total_;
    }
    [[nodiscard]] bool capped() const {
        return capped_;
    }
    [[nodiscard]] QString problem() const {
        return problem_;
    }
    [[nodiscard]] bool problemBelow() const {
        return problem_below_;
    }
    [[nodiscard]] bool homeLoading() const {
        return home_loading_;
    }
    [[nodiscard]] CardModel* results() {
        return &results_;
    }
    [[nodiscard]] CardModel* recentEpisodes() {
        return &recent_episodes_;
    }

    // What a card already knows of an anime, to paint its page at once.
    [[nodiscard]] library::AnimeRecord hint(const QString& slug) const;

    // How long ago, cut down: "ahora", "hace 29 min", "hace 3 h", "ayer", "hace 4 días", "2 oct".
    [[nodiscard]] static QString ago(const QDateTime& when, const QDateTime& now);
    // A card's line: "TV · 2026"; a recent episode's, "Ep. 4 · hace 3 h".
    [[nodiscard]] static QString captionOf(const api::AnimeSummary& anime);
    [[nodiscard]] static QString captionOf(const api::Episode& episode, const QDateTime& now);

    Q_INVOKABLE void start();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearFilters();
    // The whole catalog searched for `text`, without the filters.
    Q_INVOKABLE void searchFor(const QString& text);

signals:
    void queryChanged();
    void genresChanged();
    void stateChanged();

private:
    void fetchHome();
    void showRecent();
    void fetch(int page);
    void remember(const api::AnimeSummary& anime);

    api::ApiClient& api_;
    api::CatalogQuery query_;
    QVariantList genres_;
    CatalogCards results_;
    CatalogCards recent_episodes_;
    QList<api::RecentEpisode> recent_;
    QHash<QString, api::AnimeSummary> known_;
    QTimer debounce_;
    // The recent episodes say how long ago they came out: their words age with the clock.
    QTimer clock_;
    int page_ = 0;
    int total_pages_ = 0;
    int total_ = 0;
    bool capped_ = false;
    bool loading_ = false;
    bool refreshing_ = false;
    bool started_ = false;
    bool home_loading_ = false;
    bool home_failed_ = false;
    QString problem_;
    bool problem_below_ = false;
    quint64 generation_ = 0;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_CATALOG_VIEW_H
