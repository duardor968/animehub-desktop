#ifndef ANIMEHUB_CORE_API_TYPES_H
#define ANIMEHUB_CORE_API_TYPES_H

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

// Value types for the AnimeHub REST contract (`animehub-api/openapi.json`, /api/v1).
namespace animehub::api {

enum class AnimeStatus { Unknown, Airing, Finished, Upcoming };
enum class Provider { Mega, Pixeldrain, Mp4Upload, OneFichier };
enum class Audio { Sub, Dub };

enum class RelationKind {
    Prequel,
    Sequel,
    MainStory,
    FullStory,
    SideStory,
    SpinOff,
    Summary,
    Alternative,
    AlternativeSetting,
    Other,
};

struct Category {
    QString id;
    QString name;
    QString slug;
};

// One candidate of a `srcset`: the image and its intrinsic width.
struct ImageCandidate {
    QUrl url;
    int width = 0;
};

struct AnimeSummary {
    QString id;
    QString slug;
    QString title;
    QString synopsis;
    QUrl posterUrl;
    QUrl backdropUrl;
    QList<ImageCandidate> backdropCandidates;
    std::optional<Category> category;
    AnimeStatus status = AnimeStatus::Unknown;
    QDate startDate;
    bool mature = false;

    // Smallest backdrop at least `width` pixels wide, else the widest one.
    [[nodiscard]] QUrl backdropFor(int width) const;
};

struct FeaturedAnime : AnimeSummary {
    QList<Category> genres;
    std::optional<int> episodeCount;
    QUrl trailerUrl;
};

struct Relation {
    RelationKind kind = RelationKind::Other;
    AnimeSummary anime;
    int position = 0;
};

struct AnimeDetail : AnimeSummary {
    QString alternativeTitle;
    QUrl trailerUrl;
    QDate endDate;
    QDateTime nextEpisodeAt;
    std::optional<int> episodeCount;
    std::optional<double> score;
    std::optional<int> votes;
    QUrl sourceUrl;
    QList<Category> genres;
    QList<Relation> relations;
    // Source -> this anime, preserving the provider's original kind (never inverted).
    QList<Relation> incomingRelations;
};

struct Episode {
    QString id;
    double number = 0;
    QString title;
    QUrl imageUrl;
    QDateTime publishedAt;
};

struct RecentEpisode {
    AnimeSummary anime;
    Episode episode;
};

struct Home {
    QList<FeaturedAnime> featured;
    QList<RecentEpisode> recentEpisodes;
    QList<AnimeSummary> recentAnime;
};

enum class CatalogOrder { Default, Score, Popular, Title, LatestAdded, LatestReleased };

struct CatalogQuery {
    QString search;
    QStringList categories;
    QStringList genres;
    std::optional<AnimeStatus> status;
    std::optional<int> minYear;
    std::optional<int> maxYear;
    QString letter;
    CatalogOrder order = CatalogOrder::Default;
    int page = 1;
};

struct CatalogPage {
    QList<AnimeSummary> items;
    int page = 1;
    int perPage = 0;
    int totalPages = 0;
    int totalRecords = 0;
    bool capped = false;
    QList<Category> categories;
    QList<Category> genres;
    QList<int> years;
};

struct EpisodePage {
    QList<Episode> items;
    int page = 1;
    int perPage = 0;
    int totalPages = 0;
    int totalRecords = 0;
    std::optional<double> firstNumber;
    std::optional<double> lastNumber;
};

struct ScheduleEntry {
    AnimeSummary anime;
    Episode latestEpisode;
    QDateTime basisPublishedAt;
    bool isFinalEpisode = false;
};

struct ResolvedLink {
    Provider provider = Provider::Mega;
    QUrl url;
};

struct ResolvedEpisode {
    double episodeNumber = 0;
    Audio audio = Audio::Sub;
    QList<ResolvedLink> links;
    QString errorCode;
};

struct Resolution {
    QString packageName;
    QList<ResolvedEpisode> episodes;
};

struct ResolveRequest {
    QList<double> episodeNumbers;
    Audio audio = Audio::Sub;
    QList<Provider> providers;
    bool refresh = false;
};

[[nodiscard]] QString providerKey(Provider provider);
[[nodiscard]] std::optional<Provider> providerFromKey(QStringView key);
[[nodiscard]] QString providerLabel(Provider provider);
[[nodiscard]] QString statusKey(AnimeStatus status);
[[nodiscard]] AnimeStatus statusFromKey(QStringView key);

} // namespace animehub::api

#endif // ANIMEHUB_CORE_API_TYPES_H
