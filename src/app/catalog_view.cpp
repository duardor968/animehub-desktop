#include "catalog_view.h"

#include "display.h"

#include <QLocale>
#include <QVariantMap>

#include <algorithm>

namespace animehub::app {
namespace {

constexpr auto kDefaultOrder = api::CatalogOrder::LatestAdded;
constexpr auto kClockInterval = std::chrono::minutes(1);

} // namespace

void CatalogCards::refreshOwned() {
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {OwnedRole});
    }
}

QVariant CatalogCards::data(const QModelIndex& index, int role) const {
    if (role == OwnedRole) {
        return index.isValid() && index.row() < rowCount() &&
               library_.contains(cards().at(index.row()).slug);
    }
    return CardModel::data(index, role);
}

QHash<int, QByteArray> CatalogCards::roleNames() const {
    auto names = CardModel::roleNames();
    names.insert(OwnedRole, "owned");
    return names;
}

CatalogView::CatalogView(api::ApiClient& api, const LibraryModel& library, QObject* parent)
    : QObject(parent), api_(api), results_(library), recent_episodes_(library) {
    query_.order = kDefaultOrder;
    debounce_.setSingleShot(true);
    debounce_.setInterval(350);
    connect(&debounce_, &QTimer::timeout, this, &CatalogView::reload);
    clock_.setInterval(kClockInterval);
    connect(&clock_, &QTimer::timeout, this, &CatalogView::showRecent);
    connect(&library, &LibraryModel::membershipChanged, this, [this] {
        results_.refreshOwned();
        recent_episodes_.refreshOwned();
    });
}

QString CatalogView::ago(const QDateTime& when, const QDateTime& now) {
    const auto seconds = when.secsTo(now);
    if (seconds < 60) {
        return QStringLiteral("ahora");
    }
    if (seconds < 3600) {
        return QStringLiteral("hace %1 min").arg(seconds / 60);
    }
    if (seconds < 24 * 3600) {
        return QStringLiteral("hace %1 h").arg(seconds / 3600);
    }
    const auto date = when.toLocalTime().date();
    const auto today = now.toLocalTime().date();
    const auto days = date.daysTo(today);
    if (days <= 1) {
        return QStringLiteral("ayer");
    }
    if (days < 7) {
        return QStringLiteral("hace %1 días").arg(days);
    }
    return QLocale(QLocale::Spanish, QLocale::Spain)
        .toString(date, date.year() == today.year() ? QStringLiteral("d MMM")
                                                    : QStringLiteral("d MMM yyyy"));
}

QString CatalogView::captionOf(const api::AnimeSummary& anime) {
    QStringList parts;
    if (anime.category) {
        // "TV Anime": everything here is anime.
        auto name = anime.category->name;
        if (name.endsWith(QStringLiteral(" Anime"))) {
            name.chop(6);
        }
        parts.append(name);
    }
    if (anime.startDate.isValid()) {
        parts.append(QString::number(anime.startDate.year()));
    }
    return parts.join(QStringLiteral(" · "));
}

QString CatalogView::captionOf(const api::Episode& episode, const QDateTime& now) {
    const auto number = display::shortEpisode(episode.number);
    return episode.publishedAt.isValid()
               ? number + QStringLiteral(" · ") + ago(episode.publishedAt, now)
               : number;
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
        // Back on the page: how long ago the episodes came out has moved on.
        showRecent();
        return;
    }
    started_ = true;
    fetchHome();
    reload();
}

void CatalogView::fetchHome() {
    home_loading_ = true;
    home_failed_ = false;
    emit stateChanged();
    api_.home(this, [this](const api::Result<api::Home>& home) {
        home_loading_ = false;
        if (!home) {
            // The shelf stays away; the catalog below says whether the connection is the reason.
            home_failed_ = true;
            emit stateChanged();
            return;
        }
        recent_ = home->value.recentEpisodes;
        for (const auto& recent : recent_) {
            remember(recent.anime);
        }
        showRecent();
        if (!recent_.isEmpty()) {
            clock_.start();
        }
        emit stateChanged();
    });
}

// Only the words that changed reach the shelf.
void CatalogView::showRecent() {
    const auto now = QDateTime::currentDateTimeUtc();
    QList<Card> cards;
    for (const auto& recent : recent_) {
        cards.append({.slug = recent.anime.slug,
                      .title = recent.anime.title,
                      .posterUrl = recent.anime.posterUrl,
                      .caption = captionOf(recent.episode, now),
                      .episode = recent.episode.number});
    }
    const auto same = [](const Card& a, const Card& b) {
        return a.slug == b.slug && a.title == b.title && a.posterUrl == b.posterUrl &&
               a.caption == b.caption && a.episode == b.episode;
    };
    if (!std::ranges::equal(cards, recent_episodes_.cards(), same)) {
        recent_episodes_.setCards(std::move(cards));
    }
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

// The order is the whole catalog's own: it does not take the recent episodes away.
bool CatalogView::browsing() const {
    return !query_.search.simplified().isEmpty() || query_.status.has_value() ||
           !query_.genres.isEmpty();
}

void CatalogView::clearFilters() {
    query_.search.clear();
    query_.status.reset();
    query_.genres.clear();
    emit queryChanged();
    reload();
}

// The results on view stay until the new ones arrive, with their count.
void CatalogView::reload() {
    debounce_.stop();
    if (home_failed_) {
        fetchHome();
    }
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
    problem_below_ = false;
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
            if (!result) {
                // A first page that failed leaves what was on view: the previous results,
                // dimmed, or this query's from the cache, with the reason above them.
                problem_below_ = page > 1;
                problem_ =
                    result.error().kind == api::ApiError::Kind::RateLimited
                        ? QStringLiteral("Demasiadas búsquedas seguidas. Espera un momento.")
                        : QStringLiteral("No se pudo cargar el catálogo. Revisa la conexión.");
                emit stateChanged();
                return;
            }
            if (page == 1) {
                refreshing_ = false;
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
