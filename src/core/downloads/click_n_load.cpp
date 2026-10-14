#include "click_n_load.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace animehub::downloads {

ClickNLoad::ClickNLoad(QNetworkAccessManager& network, QObject* parent, QUrl base)
    : QObject(parent), network_(network), base_(std::move(base)) {}

void ClickNLoad::send(const QString& package, const QList<QUrl>& links, QObject* context,
                      std::function<void(const std::expected<void, Failure>&)> done) {
    // JDownloader answers its root with its own name; anything else is not it.
    QNetworkRequest health(base_);
    health.setTransferTimeout(5000);
    auto* probe = network_.get(health);
    connect(probe, &QNetworkReply::finished, probe, &QObject::deleteLater);
    connect(probe, &QNetworkReply::finished, context,
            [this, probe, package, links, context, done = std::move(done)] {
                if (probe->error() != QNetworkReply::NoError) {
                    const bool answered =
                        probe->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
                    done(std::unexpected(answered ? Failure::Foreign : Failure::Unreachable));
                    return;
                }
                if (!probe->read(4096).toLower().contains("jdownloader")) {
                    done(std::unexpected(Failure::Foreign));
                    return;
                }
                QStringList urls;
                for (const auto& link : links) {
                    urls.append(link.toString(QUrl::FullyEncoded));
                }
                // Encoded by hand: QUrlQuery leaves '+' as is, which forms read as a space.
                const QByteArray form =
                    "urls=" + QUrl::toPercentEncoding(urls.join(QLatin1Char('\n'))) +
                    "&package=" + QUrl::toPercentEncoding(package) + "&source=AnimeHub";
                QNetworkRequest add(base_.resolved(QUrl(QStringLiteral("add"))));
                add.setHeader(QNetworkRequest::ContentTypeHeader,
                              QByteArrayLiteral("application/x-www-form-urlencoded"));
                add.setTransferTimeout(8000);
                auto* reply = network_.post(add, form);
                connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
                connect(reply, &QNetworkReply::finished, context, [reply, done] {
                    if (reply->error() != QNetworkReply::NoError) {
                        done(std::unexpected(Failure::Rejected));
                        return;
                    }
                    done({});
                });
            });
}

} // namespace animehub::downloads
