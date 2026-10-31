#ifndef ANIMEHUB_APP_DISPLAY_H
#define ANIMEHUB_APP_DISPLAY_H

#include "api/api_types.h"
#include "downloads/download_types.h"

#include <QString>

// Spanish wording shared by the views: numbers, sizes, durations and states.
namespace animehub::app::display {

[[nodiscard]] QString episodeNumber(double number); // "12", "12,5"
[[nodiscard]] QString episode(double number);       // "Episodio 12"
[[nodiscard]] QString shortEpisode(double number);  // "Ep. 12"
[[nodiscard]] QString bytes(qint64 value);          // "350 MB", "1,2 GB"
[[nodiscard]] QString speed(qint64 bytes_per_second);
[[nodiscard]] QString clock(qint64 ms);          // "8:12", "1:02:03"
[[nodiscard]] QString remaining(qint64 seconds); // "4 min"
[[nodiscard]] QString status(api::AnimeStatus status);
// What the work is, as cards name it: "TV" for "TV Anime", since everything here is anime.
[[nodiscard]] QString category(const QString& name);
[[nodiscard]] QString provider(api::Provider provider);
[[nodiscard]] QString issue(downloads::TaskIssue issue);
// As above, naming the server a connection issue is about: "Se cortó la conexión con MEGA.".
[[nodiscard]] QString issue(const downloads::Task& task);
// Where a download is, worded the same in every view: "En pausa · 143 MB de 348 MB".
[[nodiscard]] QString download(const downloads::Task& task);
// When a waiting download tries again: "Se reintenta a las 11:40".
[[nodiscard]] QString retry(const QDateTime& at);
[[nodiscard]] QString time(const QDateTime& at); // "15:20"
[[nodiscard]] QString date(const QDateTime& at); // "25 oct", "25 oct 2025" another year
// MEGA's quota and what the episode does meanwhile: "MEGA llegó a su límite · se libera a las
// 15:20 · sigue con Pixeldrain"; without MEGA's time, "MEGA llegó a su límite · sigue con…".
[[nodiscard]] QString megaQuota(const QDateTime& until, bool known, const QString& meanwhile);

} // namespace animehub::app::display

#endif // ANIMEHUB_APP_DISPLAY_H
