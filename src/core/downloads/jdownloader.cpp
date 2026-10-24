#include "jdownloader.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkReply>

namespace animehub::downloads::jd {
bool isLocalEndpoint(const QUrl& endpoint) {
    return endpoint.isValid() && endpoint.scheme() == u"http" &&
           QHostAddress(endpoint.host()).isLoopback() && endpoint.userInfo().isEmpty() &&
           !endpoint.hasQuery() && !endpoint.hasFragment() &&
           (endpoint.path().isEmpty() || endpoint.path() == u"/");
}

QJsonArray ids(const QList<qint64>& values) {
    QJsonArray result;
    for (const auto id : values)
        if (id > 0)
            result.append(id);
    return result;
}

void request(QNetworkAccessManager& network, myjd::Client* cloud, const Task& task,
             const QString& path, const QJsonArray& params, QObject* context,
             myjd::Client::Callback<QJsonValue> done) {
    if (task.engine == DownloadEngine::JDownloaderCloud) {
        if (!cloud) {
            done(std::unexpected(myjd::Failure::WrongCredentials));
            return;
        }
        cloud->request(task.engineDevice, path, params, context, std::move(done));
        return;
    }
    QUrl endpoint(task.engineEndpoint);
    if (task.engine != DownloadEngine::JDownloaderLocal || !isLocalEndpoint(endpoint)) {
        done(std::unexpected(myjd::Failure::Protocol));
        return;
    }
    QByteArray query;
    for (const auto& parameter : params) {
        auto json = QJsonDocument(QJsonArray{parameter}).toJson(QJsonDocument::Compact);
        if (!query.isEmpty())
            query += '&';
        query += QUrl::toPercentEncoding(QString::fromUtf8(json.mid(1, json.size() - 2)));
    }
    endpoint.setPath(path);
    endpoint.setQuery(QString::fromLatin1(query), QUrl::StrictMode);
    QNetworkRequest request(endpoint);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply = network.get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] {
        // Nothing listens: JDownloader is closed or its local API is off, so nothing reached it.
        if (reply->error() == QNetworkReply::ConnectionRefusedError) {
            done(std::unexpected(myjd::Failure::DeviceOffline));
            return;
        }
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            done(std::unexpected(myjd::Failure::Network));
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->read(4 * 1024 * 1024)).object();
        if (!object.contains(u"data")) {
            done(std::unexpected(myjd::Failure::Protocol));
            return;
        }
        done(object.value(u"data"));
    });
}

QStringList verifiedMedia(const QString& directory) {
    const auto root = QFileInfo(directory).canonicalFilePath();
    if (root.isEmpty())
        return {};
    QStringList result;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        const auto canonical = info.canonicalFilePath();
        if (info.size() <= 0 || !info.isReadable() ||
            !canonical.startsWith(root + u'/', Qt::CaseInsensitive))
            continue;
        const auto suffix = info.suffix().toLower();
        if (suffix == u"mkv" || suffix == u"mp4" || suffix == u"avi" || suffix == u"webm" ||
            suffix == u"m4v" || suffix == u"ts")
            result.append(canonical);
    }
    result.sort();
    return result;
}
} // namespace animehub::downloads::jd
