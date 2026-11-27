#ifndef ANIMEHUB_CORE_UPDATE_HTTP_H
#define ANIMEHUB_CORE_UPDATE_HTTP_H

#include "update_types.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <chrono>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace animehub::update {

struct HttpRangeRequest {
    qint64 start = 0;
    qint64 end = -1; // inclusive; -1 means full body
};

struct HttpFetchResult {
    bool ok = false;
    int httpStatus = 0;
    QByteArray body;
    QString errorCode;
    QString errorMessage;
    QUrl finalUrl;
};

// Single-transfer HTTP helper enforcing UrlPolicy (HTTPS-only + host allowlist) on every
// initial URL and manual redirect hop, with mirror failover and retry from original URL
// (so expired CDN JWT redirects are re-resolved cleanly).
class UpdateHttpRequest final : public QObject {
    Q_OBJECT

public:
    using DoneCallback = std::function<void(HttpFetchResult)>;
    // Optional streaming sink: receives chunks as they arrive. Return false to abort transfer.
    // `http_status` is 200 or 206.
    using StreamChunkCallback = std::function<bool(int http_status, const QByteArray& chunk)>;
    using ProgressCallback = std::function<void(qint64 bytes_received, qint64 bytes_total)>;

    UpdateHttpRequest(QNetworkAccessManager& network, UrlPolicy policy, QObject* parent = nullptr);
    ~UpdateHttpRequest() override;

    void setTimeout(std::chrono::milliseconds timeout) {
        timeout_ = timeout;
    }
    void setMaxRetriesPerMirror(int retries) {
        max_retries_per_mirror_ = retries;
    }
    void setUserAgent(QByteArray user_agent) {
        user_agent_ = std::move(user_agent);
    }

    void start(QList<QUrl> urls, std::optional<HttpRangeRequest> range, DoneCallback done,
               ProgressCallback progress = {}, StreamChunkCallback stream_chunk = {});

    void cancel();
    [[nodiscard]] bool isActive() const {
        return active_;
    }

private:
    void tryCurrentMirror();
    void sendUrl(const QUrl& url, int redirects_remaining);
    void onReadyRead();
    void onFinished();
    void onTimeout();
    void failOrRetry(int status, const QString& code, const QString& message);
    void finish(HttpFetchResult result);
    void cleanupReply();

    QNetworkAccessManager& network_;
    UrlPolicy policy_;
    QByteArray user_agent_{"AnimeHub-Desktop-Updater"};
    std::chrono::milliseconds timeout_{30'000};
    int max_retries_per_mirror_ = 1;

    QList<QUrl> urls_;
    std::optional<HttpRangeRequest> range_;
    DoneCallback done_;
    ProgressCallback progress_;
    StreamChunkCallback stream_chunk_;

    qsizetype mirror_index_ = 0;
    int retry_index_ = 0;
    int redirects_remaining_ = 5;
    bool active_ = false;
    bool cancelled_ = false;
    bool aborted_by_stream_ = false;

    QPointer<QNetworkReply> reply_;
    QTimer* timer_ = nullptr;
    QByteArray buffer_;
    qint64 bytes_received_ = 0;
    qint64 expected_total_ = -1;
};

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_HTTP_H
