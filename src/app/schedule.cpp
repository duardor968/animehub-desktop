#include "schedule.h"

#include "display.h"

#include <QLocale>

#include <cmath>

namespace animehub::app::schedule {
namespace {

constexpr qint64 kWeekSeconds = 7LL * 24 * 3600;
constexpr qint64 kMaxAgeSeconds = 21LL * 24 * 3600;
// Past its hour, an episode is still on its way for a while before it counts as late.
constexpr qint64 kGraceSeconds = 3LL * 3600;

} // namespace

std::optional<Next> next(const api::ScheduleEntry& slot, const QDateTime& now) {
    const auto& basis = slot.basisPublishedAt;
    const auto latest = slot.latestEpisode.number;
    if (slot.isFinalEpisode || slot.anime.status == api::AnimeStatus::Finished ||
        !basis.isValid() || basis > now || basis.secsTo(now) > kMaxAgeSeconds ||
        latest != std::trunc(latest)) {
        return std::nullopt;
    }
    const auto at = basis.addSecs(kWeekSeconds);
    return Next{.number = latest + 1, .at = at, .late = now > at.addSecs(kGraceSeconds)};
}

QString text(const Next& next, const QDateTime& now) {
    const auto episode = display::shortEpisode(next.number);
    if (next.late) {
        return QStringLiteral("retrasado · ") + episode;
    }
    if (now >= next.at) {
        return QStringLiteral("ahora · ") + episode;
    }
    static const QLocale kSpanish(QLocale::Spanish, QLocale::Spain);
    const auto local = next.at.toLocalTime();
    const auto days = now.toLocalTime().date().daysTo(local.date());
    const auto day = days == 0   ? QStringLiteral("hoy")
                     : days == 1 ? QStringLiteral("mañana")
                                 : kSpanish.dayName(local.date().dayOfWeek());
    return day + QLatin1Char(' ') + local.toString(QStringLiteral("HH:mm")) +
           QStringLiteral(" · ") + episode;
}

QString day(const Next& next, const QDateTime& now) {
    if (next.late) {
        return QStringLiteral("Retrasados");
    }
    if (now >= next.at) {
        return QStringLiteral("Ahora");
    }
    static const QLocale kSpanish(QLocale::Spanish, QLocale::Spain);
    const auto local = next.at.toLocalTime();
    const auto days = now.toLocalTime().date().daysTo(local.date());
    if (days == 0) {
        return QStringLiteral("Hoy");
    }
    if (days == 1) {
        return QStringLiteral("Mañana");
    }
    auto name = kSpanish.dayName(local.date().dayOfWeek());
    name[0] = name[0].toUpper();
    return name;
}

QString slot(const Next& next, const QDateTime& now) {
    const auto episode = display::shortEpisode(next.number);
    if (next.late || now >= next.at) {
        return episode;
    }
    return next.at.toLocalTime().toString(QStringLiteral("HH:mm")) + QStringLiteral(" · ") +
           episode;
}

} // namespace animehub::app::schedule
