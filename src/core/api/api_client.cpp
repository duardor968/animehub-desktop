#include "api_client.h"

#include "api_json.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace animehub::api {
namespace {

constexpr qint64 kMaxBodyBytes = 16 * 1024 * 1024;
constexpr int kSearchLimit = 100;
constexpr int kMaxCatalogPage = 500;

QString statusQueryValue(AnimeStatus status) {
    switch (status) {
    case AnimeStatus::Airing:
        return QStringLiteral("emision");
    case AnimeStatus::Finished:
        return QStringLiteral("finalizado");
    case AnimeStatus::Upcoming:
        return QStringLiteral("proximamente");
    case AnimeStatus::Unknown:
        break;
    }
    return {};
}

QString orderQueryValue(CatalogOrder order) {
    switch (order) {
    case CatalogOrder::Score:
        return QStringLiteral("score");
    case CatalogOrder::Popular:
        return QStringLiteral("popular");
    case CatalogOrder::Title:
        return QStringLiteral("title");
    case CatalogOrder::LatestAdded:
        return QStringLiteral("latest_added");
    case CatalogOrder::LatestReleased:
        return QStringLiteral("latest_released");
    case CatalogOrder::Default:
        break;
    }
    return {};
}

std::chrono::seconds retryAfter(const QNetworkReply& reply) {
    bool ok = false;
    const auto seconds = reply.rawHeader("Retry-After").trimmed().toLongLong(&ok);
    return ok && seconds > 0 ? std::chrono::seconds(std::min<qint64>(seconds, 3600))
                             : std::chrono::seconds(30);
}

ApiError errorFrom(QNetworkReply& reply, const QByteArray& body) {
    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    ApiError error;
    error.httpStatus = status;
    if (status == 0) {
        error.kind = reply.error() == QNetworkReply::OperationCanceledError ||
                             reply.error() == QNetworkReply::TimeoutError
                         ? ApiError::Kind::Timeout
                         : ApiError::Kind::Offline;
        error.message = reply.errorString();
        return error;
    }
    error.kind = status == 404   ? ApiError::Kind::NotFound
                 : status == 429 ? ApiError::Kind::RateLimited
                                 : ApiError::Kind::Server;
    if (error.kind == ApiError::Kind::RateLimited) {
        error.retryAfter = retryAfter(reply);
    }
    const auto problem = json::decodeProblem(body);
    error.message = problem ? (problem->detail.isEmpty() ? problem->title : problem->detail)
                            : QStringLiteral("HTTP %1").arg(status);
    return error;
}

} // namespace

ApiClient::ApiClient(QNetworkAccessManager& network, Options options, QObject* parent)
    : QObject(parent), network_(network), options_(std::move(options)),
      cache_(options_.cacheDirectory) {
    if (!options_.baseUrl.path().endsWith(QLatin1Char('/'))) {
        options_.baseUrl.setPath(options_.baseUrl.path() + QLatin1Char('/'));
    }
}

QUrl ApiClient::endpoint(const QString& relative) const {
    return options_.baseUrl.resolved(QUrl(relative));
}

QUrl ApiClient::catalogUrl(const QUrl& base, const CatalogQuery& query) {
    QUrl url = base.resolved(QUrl(QStringLiteral("catalog")));
    QUrlQuery params;
    const auto search = query.search.simplified().left(kSearchLimit);
    if (!search.isEmpty()) {
        params.addQueryItem(QStringLiteral("search"), search);
    }
    for (const auto& category : query.categories) {
        params.addQueryItem(QStringLiteral("category"), category);
    }
    for (const auto& genre : query.genres) {
        params.addQueryItem(QStringLiteral("genre"), genre);
    }
    if (query.status) {
        if (const auto value = statusQueryValue(*query.status); !value.isEmpty()) {
            params.addQueryItem(QStringLiteral("status"), value);
        }
    }
    if (query.minYear) {
        params.addQueryItem(QStringLiteral("minYear"), QString::number(*query.minYear));
    }
    if (query.maxYear) {
        params.addQueryItem(QStringLiteral("maxYear"), QString::number(*query.maxYear));
    }
    if (query.letter.size() == 1) {
        params.addQueryItem(QStringLiteral("letter"), query.letter);
    }
    if (const auto order = orderQueryValue(query.order); !order.isEmpty()) {
        params.addQueryItem(QStringLiteral("order"), order);
    }
    params.addQueryItem(QStringLiteral("page"),
                        QString::number(std::clamp(query.page, 1, kMaxCatalogPage)));
    url.setQuery(params);
    return url;
}

template <class T, class Decoder>
ApiClient::RawCallback ApiClient::decodeWith(Decoder decoder, Callback<T> done) const {
    return [decoder = std::move(decoder), done = std::move(done)](const Result<QByteArray>& raw) {
        if (!raw) {
            done(std::unexpected(raw.error()));
            return;
        }
        auto decoded = decoder(raw->value);
        if (!decoded) {
            done(std::unexpected(
                ApiError{.kind = ApiError::Kind::Contract, .message = decoded.error()}));
            return;
        }
        done(Fetched<T>{
            .value = std::move(*decoded), .origin = raw->origin, .savedAt = raw->savedAt});
    };
}

void ApiClient::home(QObject* context, Callback<Home> done, CachePolicy policy) {
    get(endpoint(QStringLiteral("home")), policy, context,
        decodeWith<Home>(&json::decodeHome, std::move(done)));
}

void ApiClient::catalog(const CatalogQuery& query, QObject* context, Callback<CatalogPage> done,
                        CachePolicy policy) {
    get(catalogUrl(options_.baseUrl, query), policy, context,
        decodeWith<CatalogPage>(&json::decodeCatalog, std::move(done)));
}

void ApiClient::suggestions(const QString& text, QObject* context,
                            Callback<QList<AnimeSummary>> done) {
    const auto query = text.simplified().left(kSearchLimit);
    if (query.size() < 2) {
        done(Fetched<QList<AnimeSummary>>{});
        return;
    }
    QUrl url = endpoint(QStringLiteral("catalog/suggestions"));
    url.setQuery(QUrlQuery{{QStringLiteral("q"), query}});
    get(url, CachePolicy::NetworkOnly, context,
        decodeWith<QList<AnimeSummary>>(&json::decodeSuggestions, std::move(done)));
}

void ApiClient::anime(const QString& slug, QObject* context, Callback<AnimeDetail> done,
                      CachePolicy policy) {
    if (!json::isSafeSlug(slug)) {
        done(std::unexpected(
            ApiError{.kind = ApiError::Kind::NotFound, .message = QStringLiteral("bad slug")}));
        return;
    }
    get(endpoint(QStringLiteral("anime/") + slug), policy, context,
        decodeWith<AnimeDetail>(&json::decodeAnime, std::move(done)));
}

void ApiClient::episodes(const QString& slug, int page, QObject* context,
                         Callback<EpisodePage> done, CachePolicy policy) {
    if (!json::isSafeSlug(slug)) {
        done(std::unexpected(
            ApiError{.kind = ApiError::Kind::NotFound, .message = QStringLiteral("bad slug")}));
        return;
    }
    QUrl url = endpoint(QStringLiteral("anime/") + slug + QStringLiteral("/episodes"));
    url.setQuery(QUrlQuery{{QStringLiteral("page"), QString::number(std::max(page, 1))}});
    get(url, policy, context, decodeWith<EpisodePage>(&json::decodeEpisodes, std::move(done)));
}

void ApiClient::schedule(QObject* context, Callback<QList<ScheduleEntry>> done,
                         CachePolicy policy) {
    get(endpoint(QStringLiteral("schedule")), policy, context,
        decodeWith<QList<ScheduleEntry>>(&json::decodeSchedule, std::move(done)));
}

void ApiClient::resolve(const QString& slug, const ResolveRequest& request, QObject* context,
                        Callback<Resolution> done) {
    if (!json::isSafeSlug(slug)) {
        done(std::unexpected(
            ApiError{.kind = ApiError::Kind::NotFound, .message = QStringLiteral("bad slug")}));
        return;
    }
    post(endpoint(QStringLiteral("anime/") + slug + QStringLiteral("/downloads/resolve")),
         json::encodeResolveRequest(request), context,
         decodeWith<Resolution>(&json::decodeResolution, std::move(done)));
}

void ApiClient::get(const QUrl& url, CachePolicy policy, QObject* context, RawCallback done) {
    if (policy == CachePolicy::CacheThenNetwork) {
        if (auto entry = cache_.read(url)) {
            done(Fetched<QByteArray>{.value = std::move(entry->body),
                                     .origin = Origin::Cache,
                                     .savedAt = entry->savedAt});
        }
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, options_.userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(options_.timeout);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    send(network_.get(request), url, policy, context, std::move(done));
}

void ApiClient::post(const QUrl& url, const QByteArray& body, QObject* context, RawCallback done) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, options_.userAgent);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(options_.timeout);
    send(network_.post(request, body), url, CachePolicy::NetworkOnly, context, std::move(done));
}

void ApiClient::send(QNetworkReply* reply, const QUrl& url, CachePolicy policy, QObject* context,
                     RawCallback done) {
    // A reply whose context died still has to be released.
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxBodyBytes) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, context,
            [this, reply, url, policy, done = std::move(done)] {
                const auto body = reply->isOpen() ? reply->readAll() : QByteArray();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
                    if (policy != CachePolicy::NetworkOnly) {
                        cache_.write(url, body);
                    }
                    done(Fetched<QByteArray>{.value = body, .origin = Origin::Network});
                    return;
                }
                auto error = errorFrom(*reply, body);
                const bool transient = error.kind == ApiError::Kind::Offline ||
                                       error.kind == ApiError::Kind::Timeout ||
                                       error.kind == ApiError::Kind::RateLimited ||
                                       error.kind == ApiError::Kind::Server;
                if (policy == CachePolicy::NetworkElseCache && transient) {
                    if (auto entry = cache_.read(url)) {
                        done(Fetched<QByteArray>{.value = std::move(entry->body),
                                                 .origin = Origin::Cache,
                                                 .savedAt = entry->savedAt});
                        return;
                    }
                }
                done(std::unexpected(std::move(error)));
            });
}

} // namespace animehub::api
