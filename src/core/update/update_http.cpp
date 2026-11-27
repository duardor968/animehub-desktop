#include "update_http.h"

#include "update_manifest.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace animehub::update {
namespace {

constexpr int kMaxRedirects = 5;

bool isRedirectStatus(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool isRetryableStatus(int status) {
    return status == 408 || status == 429 || (status >= 500 && status <= 599);
}

} // namespace

UpdateHttpRequest::UpdateHttpRequest(QNetworkAccessManager& network, UrlPolicy policy,
                                     QObject* parent)
    : QObject(parent), network_(network), policy_(std::move(policy)), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &UpdateHttpRequest::onTimeout);
}

UpdateHttpRequest::~UpdateHttpRequest() {
    cancel();
}

void UpdateHttpRequest::start(QList<QUrl> urls, std::optional<HttpRangeRequest> range,
                              DoneCallback done, ProgressCallback progress,
                              StreamChunkCallback stream_chunk) {
    cancel();
    urls_ = std::move(urls);
    range_ = range;
    done_ = std::move(done);
    progress_ = std::move(progress);
    stream_chunk_ = std::move(stream_chunk);
    mirror_index_ = 0;
    retry_index_ = 0;
    active_ = true;
    cancelled_ = false;
    aborted_by_stream_ = false;

    if (urls_.isEmpty()) {
        finish(HttpFetchResult{.ok = false,
                               .errorCode = QStringLiteral("no_urls"),
                               .errorMessage = QStringLiteral("No download URLs available")});
        return;
    }
    tryCurrentMirror();
}

void UpdateHttpRequest::cancel() {
    cancelled_ = true;
    active_ = false;
    timer_->stop();
    cleanupReply();
    done_ = {};
    progress_ = {};
    stream_chunk_ = {};
}

void UpdateHttpRequest::tryCurrentMirror() {
    if (!active_ || cancelled_) {
        return;
    }
    while (mirror_index_ < urls_.size() && !isAllowedUpdateUrl(urls_[mirror_index_], policy_)) {
        ++mirror_index_;
        retry_index_ = 0;
    }
    if (mirror_index_ >= urls_.size()) {
        finish(HttpFetchResult{
            .ok = false,
            .errorCode = QStringLiteral("url_disallowed"),
            .errorMessage = QStringLiteral("All update URLs were rejected by security policy")});
        return;
    }
    sendUrl(urls_[mirror_index_], kMaxRedirects);
}

void UpdateHttpRequest::sendUrl(const QUrl& url, int redirects_remaining) {
    cleanupReply();
    buffer_.clear();
    bytes_received_ = 0;
    expected_total_ = -1;
    aborted_by_stream_ = false;
    redirects_remaining_ = redirects_remaining;

    if (!isAllowedUpdateUrl(url, policy_)) {
        failOrRetry(0, QStringLiteral("redirect_disallowed"),
                    QStringLiteral("Blocked update URL outside allowlist: %1").arg(url.host()));
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setHeader(QNetworkRequest::UserAgentHeader, user_agent_);
    if (range_ && range_->end >= range_->start) {
        const QByteArray header_value =
            "bytes=" + QByteArray::number(range_->start) + '-' + QByteArray::number(range_->end);
        req.setRawHeader("Range", header_value);
        expected_total_ = range_->end - range_->start + 1;
    }

    reply_ = network_.get(req);
    connect(reply_, &QNetworkReply::readyRead, this, &UpdateHttpRequest::onReadyRead);
    connect(reply_, &QNetworkReply::finished, this, &UpdateHttpRequest::onFinished);
    timer_->start(static_cast<int>(timeout_.count()));
}

void UpdateHttpRequest::onReadyRead() {
    if (!reply_ || !active_ || cancelled_) {
        return;
    }
    // Reset inactivity timeout as data arrives.
    timer_->start(static_cast<int>(timeout_.count()));

    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (isRedirectStatus(status)) {
        // Ignore redirect response bodies.
        reply_->readAll();
        return;
    }
    if (status != 200 && status != 206) {
        return;
    }
    const QByteArray chunk = reply_->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    bytes_received_ += chunk.size();
    if (expected_total_ < 0) {
        const QVariant len = reply_->header(QNetworkRequest::ContentLengthHeader);
        if (len.isValid()) {
            expected_total_ = len.toLongLong();
        }
    }
    if (stream_chunk_) {
        if (!stream_chunk_(status, chunk)) {
            aborted_by_stream_ = true;
            reply_->abort();
            return;
        }
    } else {
        buffer_.append(chunk);
    }
    if (progress_) {
        progress_(bytes_received_, expected_total_);
    }
}

void UpdateHttpRequest::onFinished() {
    if (!reply_ || !active_ || cancelled_) {
        return;
    }
    timer_->stop();

    QPointer<QNetworkReply> finished_reply = reply_;
    const int status = finished_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl final_url = finished_reply->url();
    const QNetworkReply::NetworkError net_err = finished_reply->error();
    const QString net_err_string = finished_reply->errorString();

    if (aborted_by_stream_) {
        cleanupReply();
        finish(HttpFetchResult{.ok = true, .httpStatus = status, .finalUrl = final_url});
        return;
    }

    if (isRedirectStatus(status)) {
        const QVariant loc = finished_reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        const QUrl target = final_url.resolved(loc.toUrl());
        cleanupReply();
        if (redirects_remaining_ <= 0) {
            failOrRetry(status, QStringLiteral("too_many_redirects"),
                        QStringLiteral("Too many redirects while downloading update"));
            return;
        }
        if (!isAllowedUpdateUrl(target, policy_)) {
            finish(HttpFetchResult{
                .ok = false,
                .httpStatus = status,
                .errorCode = QStringLiteral("redirect_disallowed"),
                .errorMessage =
                    QStringLiteral("Blocked redirect to untrusted host '%1'").arg(target.host()),
                .finalUrl = target});
            return;
        }
        sendUrl(target, redirects_remaining_ - 1);
        return;
    }

    // Drain any unread bytes for 200/206 before reading error status.
    if ((status == 200 || status == 206) && net_err == QNetworkReply::NoError) {
        const QByteArray tail = finished_reply->readAll();
        if (!tail.isEmpty()) {
            bytes_received_ += tail.size();
            if (stream_chunk_) {
                if (!stream_chunk_(status, tail)) {
                    aborted_by_stream_ = true;
                }
            } else {
                buffer_.append(tail);
            }
            if (progress_) {
                progress_(bytes_received_, expected_total_);
            }
        }
        QByteArray out_body = std::move(buffer_);
        cleanupReply();
        finish(HttpFetchResult{
            .ok = true, .httpStatus = status, .body = std::move(out_body), .finalUrl = final_url});
        return;
    }

    cleanupReply();
    const QString code =
        status > 0 ? QStringLiteral("http_%1").arg(status) : QStringLiteral("network_error");
    const QString message =
        status > 0 ? QStringLiteral("HTTP %1 from %2").arg(status).arg(final_url.host())
                   : net_err_string;
    failOrRetry(status, code, message);
}

void UpdateHttpRequest::onTimeout() {
    if (!active_ || cancelled_) {
        return;
    }
    cleanupReply();
    failOrRetry(0, QStringLiteral("timeout"), QStringLiteral("Update request timed out"));
}

void UpdateHttpRequest::failOrRetry(int status, const QString& code, const QString& message) {
    if (!active_ || cancelled_) {
        return;
    }
    const bool retryable = status == 0 || isRetryableStatus(status);
    if (retryable && retry_index_ < max_retries_per_mirror_) {
        ++retry_index_;
        // Always restart from the original mirror URL so expired CDN redirects re-resolve cleanly.
        tryCurrentMirror();
        return;
    }
    ++mirror_index_;
    retry_index_ = 0;
    if (mirror_index_ < urls_.size()) {
        tryCurrentMirror();
        return;
    }
    finish(HttpFetchResult{
        .ok = false, .httpStatus = status, .errorCode = code, .errorMessage = message});
}

void UpdateHttpRequest::finish(HttpFetchResult result) {
    active_ = false;
    timer_->stop();
    cleanupReply();
    auto cb = std::move(done_);
    done_ = {};
    progress_ = {};
    stream_chunk_ = {};
    if (cb && !cancelled_) {
        cb(std::move(result));
    }
}

void UpdateHttpRequest::cleanupReply() {
    if (reply_) {
        reply_->disconnect(this);
        if (reply_->isRunning()) {
            reply_->abort();
        }
        reply_->deleteLater();
        reply_ = nullptr;
    }
}

} // namespace animehub::update
