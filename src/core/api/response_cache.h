#ifndef ANIMEHUB_CORE_RESPONSE_CACHE_H
#define ANIMEHUB_CORE_RESPONSE_CACHE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <chrono>
#include <optional>

namespace animehub::api {

// Last good body per URL, for offline use and instant first paint. The API sends `no-store`, so
// HTTP caching does not apply; this cache only ever serves a copy explicitly marked as such.
class ResponseCache {
public:
    struct Entry {
        QByteArray body;
        QDateTime savedAt;
    };

    // An empty directory disables the cache.
    explicit ResponseCache(QString directory);

    [[nodiscard]] std::optional<Entry> read(const QUrl& url) const;
    void write(const QUrl& url, const QByteArray& body) const;
    // Drops copies older than `age`; returns how many were removed.
    int prune(std::chrono::hours age) const;

private:
    [[nodiscard]] QString pathFor(const QUrl& url) const;

    QString directory_;
};

} // namespace animehub::api

#endif // ANIMEHUB_CORE_RESPONSE_CACHE_H
