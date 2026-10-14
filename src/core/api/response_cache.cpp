#include "response_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimeZone>

namespace animehub::api {

namespace {
constexpr qint64 kMaxEntryBytes = 8 * 1024 * 1024;
}

ResponseCache::ResponseCache(QString directory) : directory_(std::move(directory)) {
    if (!directory_.isEmpty()) {
        QDir().mkpath(directory_);
    }
}

QString ResponseCache::pathFor(const QUrl& url) const {
    const auto key =
        QCryptographicHash::hash(url.toEncoded(QUrl::FullyEncoded), QCryptographicHash::Sha256)
            .toHex();
    return directory_ + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".json");
}

std::optional<ResponseCache::Entry> ResponseCache::read(const QUrl& url) const {
    if (directory_.isEmpty()) {
        return std::nullopt;
    }
    QFile file(pathFor(url));
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxEntryBytes) {
        return std::nullopt;
    }
    return Entry{.body = file.readAll(), .savedAt = QFileInfo(file).lastModified(QTimeZone::UTC)};
}

void ResponseCache::write(const QUrl& url, const QByteArray& body) const {
    if (directory_.isEmpty() || body.size() > kMaxEntryBytes) {
        return;
    }
    QSaveFile file(pathFor(url));
    if (file.open(QIODevice::WriteOnly) && file.write(body) == body.size()) {
        file.commit();
    }
}

int ResponseCache::prune(std::chrono::hours age) const {
    if (directory_.isEmpty()) {
        return 0;
    }
    const auto limit = QDateTime::currentDateTimeUtc().addSecs(
        -std::chrono::duration_cast<std::chrono::seconds>(age).count());
    int removed = 0;
    QDirIterator it(directory_, {QStringLiteral("*.json")}, QDir::Files);
    while (it.hasNext()) {
        const auto info = it.nextFileInfo();
        if (info.lastModified(QTimeZone::UTC) < limit && QFile::remove(info.filePath())) {
            ++removed;
        }
    }
    return removed;
}

} // namespace animehub::api
