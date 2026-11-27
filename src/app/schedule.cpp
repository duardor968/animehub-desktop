#include "schedule.h"

#include "display.h"

#include <QLocale>
#include <QTimeZone>

#include <cmath>

namespace animehub::app::schedule {
namespace {

constexpr qint64 kMaxAgeSeconds = 21LL * 24 * 3600;
// Past its hour, an episode stays upcoming for a 3-hour grace window before it counts as late.
constexpr qint64 kGraceSeconds = 3LL * 3600;

QString shortWeekday(int day_of_week) {
    static const QLocale kSpanish(QLocale::Spanish, QLocale::Spain);
    auto name = kSpanish.dayName(day_of_week, QLocale::ShortFormat);
    if (name.endsWith(QLatin1Char('.'))) {
        name.chop(1);
    }
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

} // namespace

std::optional<Next> next(const api::ScheduleEntry& slot, const QDateTime& now) {
    const auto& basis = slot.basisPublishedAt;
    const auto latest = slot.latestEpisode.number;
    if (slot.isFinalEpisode || slot.anime.status == api::AnimeStatus::Finished ||
        !basis.isValid() || basis > now || basis.secsTo(now) > kMaxAgeSeconds ||
        latest != std::trunc(latest)) {
        return std::nullopt;
    }
    // Project a calendar week in the local (or explicit) time zone so DST changes keep the hour.
    const auto zoned = basis.timeSpec() == Qt::TimeZone ? basis : basis.toLocalTime();
    const auto at = zoned.addDays(7);
    return Next{.number = latest + 1, .at = at, .late = now >= at.addSecs(kGraceSeconds)};
}

std::optional<Next> agenda(const api::ScheduleEntry& slot, const QDateTime& now) {
    const auto& basis = slot.basisPublishedAt;
    const auto latest = slot.latestEpisode.number;
    // Out today: the day's episode, not next week's. A finished series shows only its finale.
    if (basis.isValid() && basis <= now && basis.toLocalTime().date() == now.toLocalTime().date() &&
        (slot.isFinalEpisode || slot.anime.status != api::AnimeStatus::Finished)) {
        return Next{.number = latest, .at = basis, .aired = true, .finale = slot.isFinalEpisode};
    }
    return next(slot, now);
}

namespace {

// Out, due or late: today's, even when it was due before midnight.
bool today(const Next& next, const QDateTime& now) {
    return next.aired || now >= next.at || next.at.toLocalTime().date() == now.toLocalTime().date();
}

} // namespace

bool isToday(const Next& next, const QDateTime& now) {
    return today(next, now);
}

QString weekdayName(int weekday) {
    if (weekday < 0 || weekday > 6) {
        return {};
    }
    static const QLocale kSpanish(QLocale::Spanish, QLocale::Spain);
    auto name = kSpanish.dayName(weekday + 1, QLocale::LongFormat);
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

QString todayHeading(const QDate& today_date) {
    if (!today_date.isValid()) {
        return QStringLiteral("Hoy");
    }
    const auto name = weekdayName(today_date.dayOfWeek() - 1);
    return name.isEmpty() ? QStringLiteral("Hoy") : QStringLiteral("Hoy · ") + name;
}

std::optional<WeeklySlot> weekly(const api::ScheduleEntry& slot, const QDateTime& now, bool stale) {
    const auto& basis = slot.basisPublishedAt;
    const auto latest = slot.latestEpisode.number;
    if (!basis.isValid() || !now.isValid() || basis > now || basis.secsTo(now) > kMaxAgeSeconds) {
        return std::nullopt;
    }
    const auto to_zone = [&now](const QDateTime& dt) {
        return now.timeSpec() == Qt::TimeZone ? dt.toTimeZone(now.timeZone()) : dt.toLocalTime();
    };
    const auto local_now = to_zone(now);
    const auto local_basis = to_zone(basis);
    const int today_weekday = local_now.date().dayOfWeek() - 1;
    const int slot_weekday = local_basis.date().dayOfWeek() - 1;
    const QDate week_start = local_now.date().addDays(-today_weekday);
    const bool aired_this_week = local_basis.date() >= week_start;
    const int slot_minutes = local_basis.time().hour() * 60 + local_basis.time().minute();
    const QString time_text = QStringLiteral("%1:%2")
                                  .arg(local_basis.time().hour(), 2, 10, QLatin1Char('0'))
                                  .arg(local_basis.time().minute(), 2, 10, QLatin1Char('0'));
    const bool is_today = (slot_weekday == today_weekday);

    if (slot.isFinalEpisode) {
        if (!aired_this_week) {
            return std::nullopt;
        }
        return WeeklySlot{.number = latest,
                          .weekday = slot_weekday,
                          .slotMinutes = slot_minutes,
                          .time = time_text,
                          .state = QStringLiteral("finale"),
                          .stateLabel = QStringLiteral("Final"),
                          .isToday = is_today};
    }
    if (slot.anime.status == api::AnimeStatus::Finished) {
        return std::nullopt;
    }
    if (aired_this_week) {
        return WeeklySlot{.number = latest,
                          .weekday = slot_weekday,
                          .slotMinutes = slot_minutes,
                          .time = time_text,
                          .state = QStringLiteral("aired"),
                          .stateLabel = QStringLiteral("Emitido"),
                          .isToday = is_today};
    }
    if (stale || latest != std::trunc(latest)) {
        return WeeklySlot{.number = latest,
                          .weekday = slot_weekday,
                          .slotMinutes = slot_minutes,
                          .time = time_text,
                          .state = {},
                          .stateLabel = {},
                          .isToday = is_today};
    }
    // Wall-clock seconds in the local calendar week keep the 3-hour grace across DST shifts.
    const qint64 now_week_secs =
        static_cast<qint64>(today_weekday) * 86400LL + QTime(0, 0).secsTo(local_now.time());
    const qint64 slot_week_secs =
        static_cast<qint64>(slot_weekday) * 86400LL + QTime(0, 0).secsTo(local_basis.time());
    const bool is_late = now_week_secs >= slot_week_secs + kGraceSeconds;
    return WeeklySlot{.number = latest + 1,
                      .weekday = slot_weekday,
                      .slotMinutes = slot_minutes,
                      .time = time_text,
                      .state = is_late ? QStringLiteral("late") : QStringLiteral("upcoming"),
                      .stateLabel =
                          is_late ? QStringLiteral("Retrasado") : QStringLiteral("Próximo"),
                      .isToday = is_today};
}

QString day(const Next& next, const QDateTime& now) {
    if (today(next, now)) {
        return QStringLiteral("Hoy");
    }
    return display::day(next.at.toLocalTime().date(), now.toLocalTime().date());
}

QString badge(const Next& next, const QDateTime& now) {
    if (next.aired) {
        return next.finale ? QStringLiteral("Final") : QStringLiteral("Emitido");
    }
    if (next.late) {
        return QStringLiteral("Retrasado");
    }
    return today(next, now) ? QStringLiteral("Próximo") : QString();
}

QString timeText(const Next& next, const QDateTime& now) {
    if (!next.at.isValid()) {
        return {};
    }
    const auto at_local = next.at.toLocalTime();
    const auto days_earlier = at_local.date().daysTo(now.toLocalTime().date());
    if (days_earlier > 0 && !next.aired) {
        if (days_earlier < 7) {
            return shortWeekday(at_local.date().dayOfWeek()) + QLatin1Char(' ') +
                   display::time(next.at);
        }
        return display::when(next.at, now);
    }
    return display::time(next.at);
}

QString slot(const Next& next, const QDateTime& now) {
    const auto episode = display::shortEpisode(next.number);
    // Under "Hoy", one due on an earlier day keeps that day beside its hour.
    const bool earlier = next.at.toLocalTime().date() < now.toLocalTime().date();
    const auto line =
        (earlier && !next.aired ? display::when(next.at, now) : display::time(next.at)) +
        QStringLiteral(" · ") + episode;
    if (const auto tag = badge(next, now); !tag.isEmpty()) {
        return line + QStringLiteral(" · ") + tag;
    }
    return line;
}

QString state(const Next& next, const QDateTime& now) {
    if (next.aired) {
        return next.finale ? QStringLiteral("finale") : QStringLiteral("aired");
    }
    if (next.late) {
        return QStringLiteral("late");
    }
    return today(next, now) ? QStringLiteral("upcoming") : QString();
}

} // namespace animehub::app::schedule
