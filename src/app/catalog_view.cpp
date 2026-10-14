#include "catalog_view.h"

#include "display.h"

#include <QVariantMap>

namespace animehub::app {
namespace {

constexpr auto kDefaultOrder = api::CatalogOrder::LatestAdded;

} // namespace

CatalogView::CatalogView(api::ApiClient& api, const LibraryModel& library, QObject* parent)
    : QObject(parent), api_(api), library_(library) {
    query_.order = kDefaultOrder;
    debounce_.setSingleShot(true);
    debounce_.setInterval(350);
    connect(&debounce_, &QTimer::timeout, this, &CatalogView::reload);
    connect(&library_, &LibraryModel::membershipChanged, this, [this] {
        recaption(results_);
        recaption(recent_episodes_);
        recaption(recent_anime_);
    });
}

// "TV Anime · 2023", or "En la biblioteca" for an anime the user has; a recent episode says
// which one: "Ep. 12", "Ep. 12 · En la biblioteca".
QString CatalogView::captionOf(const api::AnimeSummary& anime, double episode) const {
    const bool owned = library_.contains(anime.slug);
    if (episode >= 0) {
        auto caption = QStringLiteral("Ep. ") + display::episodeNumber(episode);
        return owned ? caption + QStringLiteral(" · En la biblioteca") : caption;
    }
    if (owned) {
        return QStringLiteral("En la biblioteca");
    }
    QStringList parts;
    if (anime.category) {
        parts.append(anime.category->name);
    }
    if (anime.startDate.isValid()) {
        parts.append(QString::number(anime.startDate.year()));
    }
    return parts.join(QStringLiteral(" · "));
}

void CatalogView::recaption(CardModel& model) const {
    auto cards = model.cards();
    for (auto& card : cards) {
        if (const auto it = known_.find(card.slug); it != known_.end()) {
            card.caption = captionOf(*it, card.episode);
        }
    }
    model.setCards(std::move(cards));
}

void CatalogView::remember(const api::AnimeSummary& anime) {
    known_.insert(anime.slug, anime);
}

library::AnimeRecord CatalogView::hint(const QString& slug) const {
    const auto it = known_.find(slug);
    if (it == known_.end()) {
        library::AnimeRecord record;
        record.slug = slug;
        return record;
    }
    return library::snapshotOf(*it);
}

void CatalogView::start() {
    if (started_) {
        return;
    }
    started_ = true;
    api_.home(this, [this](const api::Result<api::Home>& home) {
        if (!home) {
            if (recent_episodes_.rowCount() == 0) {
                problem_ = QStringLiteral("No se pudo cargar el catálogo. Revisa la conexión.");
                emit stateChanged();
            }
            return;
        }
        QList<Card> episodes;
        for (const auto& recent : home->value.recentEpisodes) {
            remember(recent.anime);
            episodes.append({.slug = recent.anime.slug,
                             .title = recent.anime.title,
                             .posterUrl = recent.anime.posterUrl,
                             .caption = captionOf(recent.anime, recent.episode.number),
                             .episode = recent.episode.number});
        }
        recent_episodes_.setCards(std::move(episodes));
        QList<Card> added;
        for (const auto& anime : home->value.recentAnime) {
            remember(anime);
            added.append({.slug = anime.slug,
                          .title = anime.title,
                          .posterUrl = anime.posterUrl,
                          .caption = captionOf(anime)});
        }
        recent_anime_.setCards(std::move(added));
        problem_.clear();
        emit stateChanged();
    });
    reload();
}

void CatalogView::setSearch(const QString& search) {
    if (query_.search == search) {
        return;
    }
    query_.search = search;
    refreshing_ = true;
    emit queryChanged();
    emit stateChanged();
    debounce_.start();
}

void CatalogView::searchFor(const QString& text) {
    query_.search = text;
    query_.status.reset();
    query_.genres.clear();
    query_.order = kDefaultOrder;
    emit queryChanged();
    reload();
}

int CatalogView::status() const {
    if (!query_.status) {
        return 0;
    }
    switch (*query_.status) {
    case api::AnimeStatus::Airing:
        return 1;
    case api::AnimeStatus::Finished:
        return 2;
    case api::AnimeStatus::Upcoming:
        return 3;
    case api::AnimeStatus::Unknown:
        break;
    }
    return 0;
}

void CatalogView::setStatus(int status) {
    static constexpr std::array kStatuses = {api::AnimeStatus::Airing, api::AnimeStatus::Finished,
                                             api::AnimeStatus::Upcoming};
    const auto value = status >= 1 && status <= 3
                           ? std::optional(kStatuses[static_cast<std::size_t>(status - 1)])
                           : std::nullopt;
    if (query_.status == value) {
        return;
    }
    query_.status = value;
    emit queryChanged();
    reload();
}

QString CatalogView::genre() const {
    return query_.genres.value(0);
}

void CatalogView::setGenre(const QString& genre) {
    if (this->genre() == genre) {
        return;
    }
    query_.genres = genre.isEmpty() ? QStringList() : QStringList{genre};
    emit queryChanged();
    reload();
}

void CatalogView::setOrder(int order) {
    const auto value = static_cast<api::CatalogOrder>(
        std::clamp(order, 0, static_cast<int>(api::CatalogOrder::LatestReleased)));
    if (query_.order == value) {
        return;
    }
    query_.order = value;
    emit queryChanged();
    reload();
}

// Any search, filter or other order shows the matching catalog instead of the news shelves,
// which only follow the default order.
bool CatalogView::browsing() const {
    return !query_.search.simplified().isEmpty() || query_.status.has_value() ||
           !query_.genres.isEmpty() || query_.order != kDefaultOrder;
}

void CatalogView::clearFilters() {
    query_.search.clear();
    query_.status.reset();
    query_.genres.clear();
    query_.order = kDefaultOrder;
    emit queryChanged();
    reload();
}

// The results on view stay until the new ones arrive, with their count.
void CatalogView::reload() {
    debounce_.stop();
    ++generation_;
    page_ = 0;
    total_pages_ = 0;
    refreshing_ = true;
    fetch(1);
}

void CatalogView::loadMore() {
    if (!loading_ && hasMore()) {
        fetch(page_ + 1);
    }
}

void CatalogView::fetch(int page) {
    loading_ = true;
    problem_.clear();
    emit stateChanged();
    auto query = query_;
    query.page = page;
    const auto generation = generation_;
    api_.catalog(
        query, this, [this, page, generation](const api::Result<api::CatalogPage>& result) {
            if (generation != generation_) {
                return;
            }
            loading_ = false;
            if (page == 1) {
                refreshing_ = false;
            }
            if (!result) {
                problem_ =
                    result.error().kind == api::ApiError::Kind::RateLimited
                        ? QStringLiteral("Demasiadas búsquedas seguidas. Espera un momento.")
                        : QStringLiteral("No se pudo cargar el catálogo. Revisa la conexión.");
                if (page == 1) {
                    // What is on view answered another search.
                    results_.setCards({});
                    total_ = 0;
                    capped_ = false;
                }
                emit stateChanged();
                return;
            }
            const auto& value = result->value;
            QList<Card> cards;
            for (const auto& anime : value.items) {
                remember(anime);
                cards.append({.slug = anime.slug,
                              .title = anime.title,
                              .posterUrl = anime.posterUrl,
                              .caption = captionOf(anime)});
            }
            if (page == 1) {
                results_.setCards(std::move(cards));
            } else {
                results_.append(cards);
            }
            page_ = page;
            total_pages_ = value.totalPages;
            total_ = value.totalRecords;
            capped_ = value.capped;
            if (genres_.isEmpty() && !value.genres.isEmpty()) {
                for (const auto& genre : value.genres) {
                    genres_.append(QVariantMap{{QStringLiteral("slug"), genre.slug},
                                               {QStringLiteral("name"), genre.name}});
                }
                emit genresChanged();
            }
            emit stateChanged();
        });
}

} // namespace animehub::app
