#ifndef ANIMEHUB_CORE_API_JSON_H
#define ANIMEHUB_CORE_API_JSON_H

#include "api_types.h"

#include <QByteArray>
#include <QJsonObject>

#include <expected>

// Strict decoding of API responses: a missing required field or a wrong type rejects the whole
// document with the JSON path that failed, so a contract change surfaces instead of rendering
// half-empty screens.
namespace animehub::api::json {

template <class T> using Decoded = std::expected<T, QString>;

[[nodiscard]] Decoded<Home> decodeHome(const QByteArray& body);
[[nodiscard]] Decoded<CatalogPage> decodeCatalog(const QByteArray& body);
[[nodiscard]] Decoded<QList<AnimeSummary>> decodeSuggestions(const QByteArray& body);
[[nodiscard]] Decoded<AnimeDetail> decodeAnime(const QByteArray& body);
[[nodiscard]] Decoded<EpisodePage> decodeEpisodes(const QByteArray& body);
[[nodiscard]] Decoded<QList<ScheduleEntry>> decodeSchedule(const QByteArray& body);
[[nodiscard]] Decoded<Resolution> decodeResolution(const QByteArray& body);

// RFC 9457 problem details; empty when the body is not one.
struct Problem {
    QString title;
    QString detail;
};
[[nodiscard]] std::optional<Problem> decodeProblem(const QByteArray& body);

[[nodiscard]] QByteArray encodeResolveRequest(const ResolveRequest& request);

[[nodiscard]] QList<ImageCandidate> parseSrcSet(QStringView srcset);

// Slugs travel in URL paths and cache keys: ASCII letters, digits, `-`, `_` and `.` only.
[[nodiscard]] bool isSafeSlug(QStringView slug);

} // namespace animehub::api::json

#endif // ANIMEHUB_CORE_API_JSON_H
