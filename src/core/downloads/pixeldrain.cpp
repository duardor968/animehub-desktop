#include "pixeldrain.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace animehub::downloads::pixeldrain {
namespace {

bool isId(QStringView id) {
    return !id.isEmpty() && id.size() <= 32 && std::ranges::all_of(id, [](QChar ch) {
        return (ch.isLetterOrNumber() && ch.unicode() < 128) || ch == u'-' || ch == u'_';
    });
}

} // namespace

std::optional<QString> fileId(const QUrl& url) {
    const auto host = url.host().toLower();
    if (url.scheme() != u"https" ||
        (host != u"pixeldrain.com" && host != u"pixeldrain.net" && host != u"www.pixeldrain.com")) {
        return std::nullopt;
    }
    const auto parts = url.path().split(u'/', Qt::SkipEmptyParts);
    QString id;
    if (parts.size() == 2 && parts[0] == u"u") {
        id = parts[1];
    } else if (parts.size() == 3 && parts[0] == u"api" && parts[1] == u"file") {
        id = parts[2];
    }
    return isId(id) ? std::optional<QString>(id) : std::nullopt;
}

Api::Api(QNetworkAccessManager& network, QObject* parent, QUrl base)
    : QObject(parent), network_(network), base_(std::move(base)) {}

void Api::info(const QString& id, QObject* context,
               std::function<void(const std::expected<Info, Failure>&)> done) {
    if (!isId(id)) {
        done(std::unexpected(Failure{.kind = Failure::Kind::NotFound}));
        return;
    }
    QNetworkRequest request(base_.resolved(QUrl(QStringLiteral("api/file/%1/info").arg(id))));
    request.setTransferTimeout(20'000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context, [this, reply, id, done = std::move(done)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            done(std::unexpected(Failure{.kind = Failure::Kind::NotFound}));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            done(std::unexpected(Failure{.kind = status >= 500 || status == 429
                                                     ? Failure::Kind::Unavailable
                                                     : Failure::Kind::Network,
                                         .message = reply->errorString()}));
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->read(256 * 1024)).object();
        const auto size = object.value(u"size").toInteger(-1);
        if (size < 0) {
            done(std::unexpected(Failure{.kind = Failure::Kind::Protocol,
                                         .message = QStringLiteral("unexpected answer")}));
            return;
        }
        QUrl download = base_.resolved(QUrl(QStringLiteral("api/file/%1").arg(id)));
        download.setQuery(QUrlQuery{{QStringLiteral("download"), QString()}});
        done(Info{.name = object.value(u"name").toString(), .size = size, .download = download});
    });
}

} // namespace animehub::downloads::pixeldrain
