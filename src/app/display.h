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
[[nodiscard]] QString provider(api::Provider provider);
[[nodiscard]] QString issue(downloads::TaskIssue issue);
// Where a download is, worded the same in every view: "En pausa · 143 MB de 348 MB".
[[nodiscard]] QString download(const downloads::Task& task);

} // namespace animehub::app::display

#endif // ANIMEHUB_APP_DISPLAY_H
