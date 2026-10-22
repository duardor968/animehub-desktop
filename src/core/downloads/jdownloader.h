#ifndef ANIMEHUB_DOWNLOADS_JDOWNLOADER_H
#define ANIMEHUB_DOWNLOADS_JDOWNLOADER_H

#include "download_types.h"
#include "myjdownloader.h"
#include <QNetworkAccessManager>

namespace animehub::downloads::jd {
// Both transports expose the same documented, positional device API. Local mode
// never changes JD configuration and refuses non-loopback hosts and redirects.
[[nodiscard]] bool isLocalEndpoint(const QUrl& endpoint);
void request(QNetworkAccessManager& network, myjd::Client* cloud, const Task& task,
             const QString& path, const QJsonArray& params, QObject* context,
             myjd::Client::Callback<QJsonValue> done);
[[nodiscard]] QJsonArray ids(const QList<qint64>& values);
[[nodiscard]] QStringList verifiedMedia(const QString& directory);
} // namespace animehub::downloads::jd
#endif
