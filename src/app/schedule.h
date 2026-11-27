#ifndef ANIMEHUB_APP_SCHEDULE_H
#define ANIMEHUB_APP_SCHEDULE_H

#include "api/api_types.h"

#include <QDateTime>
#include <QString>

#include <optional>

// The schedule's rules (`ScheduleEntryDto` in the API contract): the next episode is the latest
// one plus one, a week after it; older slots no longer describe a weekly release. On its own day
// a slot is about the episode that day brought: once out, the agenda shows it as aired instead of
// jumping a week ahead, as AnimeHub Web's schedule does.
namespace animehub::app::schedule {

struct Next {
    double number = 0;
    QDateTime at;
    bool late = false;   // past its hour and the grace after it
    bool aired = false;  // already out today (`number` is that episode, `at` when it came out)
    bool finale = false; // aired, and the series' last one
};

// One entry in the Monday–Sunday weekly timetable: `weekday` runs 0 (Lunes) .. 6 (Domingo) in the
// local calendar week, never rotating Today to the front.
struct WeeklySlot {
    double number = 0;
    int weekday = 0;     // 0 = Monday .. 6 = Sunday
    int slotMinutes = 0; // 0..1439 local wall-clock time
    QString time;        // "HH:mm" local time
    QString state;       // "aired", "finale", "upcoming", "late", or "" when stale/special
    QString stateLabel;  // "Emitido", "Final", "Próximo", "Retrasado", or ""
    bool isToday = false;
};

// The episode a slot announces: none after a finale, for a finished series, for a slot too old
// to be weekly or after a special.
[[nodiscard]] std::optional<Next> next(const api::ScheduleEntry& slot, const QDateTime& now);
// For the week's agenda: the episode out today (local day) if there is one, `next` otherwise. Its
// days run from today to six days on, so a slot never reads as a date a week away.
[[nodiscard]] std::optional<Next> agenda(const api::ScheduleEntry& slot, const QDateTime& now);
// For the Monday–Sunday weekly board: derives the episode number and 4-state status ("Emitido",
// "Final", "Próximo", "Retrasado" — never "Ahora") for the current calendar week. When `stale` is
// true, un-aired entries keep their slot and episode but suppress predicted status (`state` empty).
[[nodiscard]] std::optional<WeeklySlot> weekly(const api::ScheduleEntry& slot, const QDateTime& now,
                                               bool stale = false);
// True when `next` belongs under "Hoy" (aired today, due today, or already past due).
[[nodiscard]] bool isToday(const Next& next, const QDateTime& now);
// Capitalized Spanish weekday name for `weekday` in 0 (Lunes) .. 6 (Domingo).
[[nodiscard]] QString weekdayName(int weekday);
// Heading for the library's today rail: e.g. "Hoy · Jueves".
[[nodiscard]] QString todayHeading(const QDate& today);
// For an agenda: the day it goes under, as display::day words it ("Hoy", "Jueves"), with what is
// out, due or late under "Hoy"; and what is left for its line. The states match AnimeHub Web:
// "09:37 · Ep. 9 · Emitido", "Final" for a finale, "22:00 · Ep. 14 · Próximo" for one still to
// come today or within the 3-hour grace window, "13:00 · Ep. 7 · Retrasado" once at or past +3h
// (with its day when that was before today), and on later days the time and episode alone.
[[nodiscard]] QString day(const Next& next, const QDateTime& now);
[[nodiscard]] QString slot(const Next& next, const QDateTime& now);
// Its state for the line's tint: "aired", "finale", "upcoming", "late" or empty.
[[nodiscard]] QString state(const Next& next, const QDateTime& now);
// Short state badge: "Final", "Emitido", "Retrasado", "Próximo", or empty for later days.
[[nodiscard]] QString badge(const Next& next, const QDateTime& now);
// Compact time label for agenda rows: "09:37", or "Vie 13:00" when due on an earlier day.
[[nodiscard]] QString timeText(const Next& next, const QDateTime& now);

} // namespace animehub::app::schedule

#endif // ANIMEHUB_APP_SCHEDULE_H
