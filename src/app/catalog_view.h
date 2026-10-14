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

// The Catalog page: what is new on AnimeHub, and search and filters over the whole catalog.
class CatalogView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY queryChanged)
    Q_PROPERTY(int status READ status WRITE setStatus NOTIFY queryChanged)
    Q_PROPERTY(QString genre READ genre WRITE setGenre NOTIFY queryChanged)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY queryChanged)
    Q_PROPERTY(bool browsing READ browsing NOTIFY queryChanged)
    Q_PROPERTY(QVariantList genres READ genres NOTIFY genresChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    // A new search or filter is on its way; the results on view are the previous ones.
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    // The source stops counting at 1000 matches: `total` is then a lower bound.
    Q_PROPERTY(bool capped READ capped NOTIFY stateChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged)
    Q_PROPERTY(CardModel* results READ results CONSTANT)
    Q_PROPERTY(CardModel* recentEpisodes READ recentEpisodes CONSTANT)
    Q_PROPERTY(CardModel* recentAnime READ recentAnime CONSTANT)

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
    [[nodiscard]] CardModel* results() {
        return &results_;
    }
    [[nodiscard]] CardModel* recentEpisodes() {
        return &recent_episodes_;
    }
    [[nodiscard]] CardModel* recentAnime() {
        return &recent_anime_;
    }

    // What a card already knows of an anime, to paint its page at once.
    [[nodiscard]] library::AnimeRecord hint(const QString& slug) const;

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
    void fetch(int page);
    void remember(const api::AnimeSummary& anime);
    void recaption(CardModel& model) const;
    [[nodiscard]] QString captionOf(const api::AnimeSummary& anime, double episode = -1) const;

    api::ApiClient& api_;
    const LibraryModel& library_;
    api::CatalogQuery query_;
    QVariantList genres_;
    CardModel results_;
    CardModel recent_episodes_;
    CardModel recent_anime_;
    QHash<QString, api::AnimeSummary> known_;
    QTimer debounce_;
    int page_ = 0;
    int total_pages_ = 0;
    int total_ = 0;
    bool capped_ = false;
    bool loading_ = false;
    bool refreshing_ = false;
    bool started_ = false;
    QString problem_;
    quint64 generation_ = 0;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_CATALOG_VIEW_H
