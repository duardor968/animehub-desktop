#include "schedule.h"

#include "display.h"

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

QString day(const Next& next, const QDateTime& now) {
    if (next.late) {
        return QStringLiteral("Retrasados");
    }
    if (now >= next.at) {
        return QStringLiteral("Ahora");
    }
    return display::day(next.at.toLocalTime().date(), now.toLocalTime().date());
}

QString slot(const Next& next, const QDateTime& now) {
    const auto episode = display::shortEpisode(next.number);
    if (next.late || now >= next.at) {
        return episode;
    }
    return display::time(next.at) + QStringLiteral(" · ") + episode;
}

} // namespace animehub::app::schedule
