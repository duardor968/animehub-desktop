#include "library_types.h"

#include <QLocale>
#include <QStringList>

#include <cmath>

namespace animehub::library {

bool LibraryEntry::finished() const {
    const int total = episodeCount.value_or(knownEpisodes);
    return status == api::AnimeStatus::Finished && total > 0 && watchedEpisodes >= total;
}

QString formatEpisodeNumber(double number) {
    if (number == std::trunc(number)) {
        return QString::number(static_cast<qint64>(number));
    }
    return QString::number(number, 'g', 6);
}

AnimeRecord snapshotOf(const api::AnimeSummary& anime) {
    AnimeRecord record;
    record.slug = anime.slug;
    record.title = anime.title;
    record.synopsis = anime.synopsis;
    record.posterUrl = anime.posterUrl;
    record.backdropUrl = anime.backdropFor(1280);
    record.category = anime.category ? anime.category->name : QString();
    record.status = anime.status;
    if (anime.startDate.isValid()) {
        record.startYear = anime.startDate.year();
    }
    return record;
}

AnimeRecord snapshotOf(const api::AnimeDetail& anime) {
    auto record = snapshotOf(static_cast<const api::AnimeSummary&>(anime));
    record.alternativeTitle = anime.alternativeTitle;
    record.episodeCount = anime.episodeCount;
    QStringList genres;
    for (const auto& genre : anime.genres) {
        genres.append(genre.name);
    }
    record.genres = genres.join(QStringLiteral(", "));
    return record;
}

} // namespace animehub::library
