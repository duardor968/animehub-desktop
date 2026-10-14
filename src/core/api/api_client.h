#ifndef ANIMEHUB_CORE_API_CLIENT_H
#define ANIMEHUB_CORE_API_CLIENT_H

#include "api_types.h"
#include "response_cache.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <chrono>
#include <expected>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace animehub::api {

struct ApiError {
    enum class Kind {
        Offline,     // no connection, DNS or TLS failure
        Timeout,     // the transfer exceeded its budget
        RateLimited, // 429; `retryAfter` says when to try again
        NotFound,    // 404
        Server,      // other HTTP failures
        Contract,    // the body does not match the published contract
    };
    Kind kind = Kind::Server;
    int httpStatus = 0;
    QString message;
    std::chrono::seconds retryAfter{0};
};

enum class Origin { Network, Cache };

template <class T> struct Fetched {
    T value;
    Origin origin = Origin::Network;
    QDateTime savedAt;
};

template <class T> using Result = std::expected<Fetched<T>, ApiError>;

enum class CachePolicy {
    NetworkOnly,      // writes are never cached; nor are answers that must be fresh
    NetworkElseCache, // the network answer, or the last saved copy when the network fails
    CacheThenNetwork, // the saved copy right away (if any), then the network answer
};

// Asynchronous client for AnimeHub API. Callbacks run on the client's thread and only while
// `context` is alive; with CacheThenNetwork a callback can run twice (cache, then network). A
// network failure after a cached delivery is still reported so the UI can say it is offline.
class ApiClient final : public QObject {
    Q_OBJECT

public:
    struct Options {
        QUrl baseUrl{QStringLiteral("https://animehub-api.duardo.dev/api/v1/")};
        QString cacheDirectory;
        QByteArray userAgent{"AnimeHub-Desktop"};
        std::chrono::milliseconds timeout{20'000};
    };

    template <class T> using Callback = std::function<void(const Result<T>&)>;

    ApiClient(QNetworkAccessManager& network, Options options, QObject* parent = nullptr);

    [[nodiscard]] const QUrl& baseUrl() const {
        return options_.baseUrl;
    }

    void home(QObject* context, Callback<Home> done,
              CachePolicy policy = CachePolicy::CacheThenNetwork);
    void catalog(const CatalogQuery& query, QObject* context, Callback<CatalogPage> done,
                 CachePolicy policy = CachePolicy::NetworkElseCache);
    void suggestions(const QString& text, QObject* context, Callback<QList<AnimeSummary>> done);
    void anime(const QString& slug, QObject* context, Callback<AnimeDetail> done,
               CachePolicy policy = CachePolicy::CacheThenNetwork);
    void episodes(const QString& slug, int page, QObject* context, Callback<EpisodePage> done,
                  CachePolicy policy = CachePolicy::NetworkElseCache);
    void schedule(QObject* context, Callback<QList<ScheduleEntry>> done,
                  CachePolicy policy = CachePolicy::NetworkElseCache);
    void resolve(const QString& slug, const ResolveRequest& request, QObject* context,
                 Callback<Resolution> done);

    [[nodiscard]] static QUrl catalogUrl(const QUrl& base, const CatalogQuery& query);

private:
    using RawCallback = std::function<void(const Result<QByteArray>&)>;

    void get(const QUrl& url, CachePolicy policy, QObject* context, RawCallback done);
    void post(const QUrl& url, const QByteArray& body, QObject* context, RawCallback done);
    void send(QNetworkReply* reply, const QUrl& url, CachePolicy policy, QObject* context,
              RawCallback done);
    [[nodiscard]] QUrl endpoint(const QString& relative) const;

    template <class T, class Decoder>
    RawCallback decodeWith(Decoder decoder, Callback<T> done) const;

    QNetworkAccessManager& network_;
    Options options_;
    ResponseCache cache_;
};

} // namespace animehub::api

#endif // ANIMEHUB_CORE_API_CLIENT_H
