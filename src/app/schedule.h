#ifndef ANIMEHUB_APP_SCHEDULE_H
#define ANIMEHUB_APP_SCHEDULE_H

#include "api/api_types.h"

#include <QDateTime>
#include <QString>

#include <optional>

// The schedule's rules (`ScheduleEntryDto` in the API contract): the next episode is the latest
// one plus one, a week after it; older slots no longer describe a weekly release.
namespace animehub::app::schedule {

struct Next {
    double number = 0;
    QDateTime at;
    bool late = false; // past its hour and the grace after it
};

// The episode a slot announces: none after a finale, for a finished series, for a slot too old
// to be weekly or after a special.
[[nodiscard]] std::optional<Next> next(const api::ScheduleEntry& slot, const QDateTime& now);
// "jueves 22:00 · Ep. 14", "mañana 09:30 · Ep. 3", "ahora · Ep. 8", "retrasado · Ep. 8".
[[nodiscard]] QString text(const Next& next, const QDateTime& now);

} // namespace animehub::app::schedule

#endif // ANIMEHUB_APP_SCHEDULE_H
