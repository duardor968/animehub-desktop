#include "library_repository.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlRecord>
#include <QTimeZone>

namespace animehub::library {
namespace {

using storage::nowMs;
using storage::StorageError;

QVariant nullable(const QDateTime& value) {
    return value.isValid() ? QVariant(value.toMSecsSinceEpoch())
                           : QVariant(QMetaType::fromType<qint64>());
}

QVariant nullable(std::optional<int> value) {
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<int>());
}

QDateTime timeAt(const QVariant& value) {
    return value.isNull() ? QDateTime()
                          : QDateTime::fromMSecsSinceEpoch(value.toLongLong(), QTimeZone::UTC);
}

std::optional<int> optionalInt(const QVariant& value) {
    return value.isNull() ? std::nullopt : std::optional<int>(value.toInt());
}

constexpr auto kAnimeColumns =
    "slug, title, alternative_title, synopsis, poster_url, backdrop_url, category, status, "
    "episode_count, genres, start_year, in_library, added_at, favorite, notify, auto_download, "
    "last_activity_at, episodes_synced_at, archived";

AnimeRecord readAnime(const QSqlQuery& query) {
    AnimeRecord record;
    record.slug = query.value(0).toString();
    record.title = query.value(1).toString();
    record.alternativeTitle = query.value(2).toString();
    record.synopsis = query.value(3).toString();
    record.posterUrl = QUrl(query.value(4).toString());
    record.backdropUrl = QUrl(query.value(5).toString());
    record.category = query.value(6).toString();
    record.status = api::statusFromKey(query.value(7).toString());
    record.episodeCount = optionalInt(query.value(8));
    record.genres = query.value(9).toString();
    record.startYear = optionalInt(query.value(10));
    record.inLibrary = query.value(11).toBool();
    record.addedAt = timeAt(query.value(12));
    record.favorite = query.value(13).toBool();
    record.notify = query.value(14).toBool();
    record.autoDownload = query.value(15).toBool();
    record.lastActivityAt = timeAt(query.value(16));
    record.episodesSyncedAt = timeAt(query.value(17));
    record.archived = query.value(18).toBool();
    return record;
}

EpisodeRecord readEpisode(const QSqlQuery& query) {
    EpisodeRecord record;
    record.number = query.value(0).toDouble();
    record.title = query.value(1).toString();
    record.imageUrl = QUrl(query.value(2).toString());
    record.publishedAt = timeAt(query.value(3));
    record.isNew = query.value(4).toBool();
    record.state = static_cast<WatchState>(std::clamp(query.value(5).toInt(), 0, 2));
    record.positionMs = query.value(6).toLongLong();
    record.durationMs = query.value(7).toLongLong();
    record.stateAt = timeAt(query.value(8));
    record.filePath = query.value(9).toString();
    record.fileSize = query.value(10).toLongLong();
    record.sourceEpisodeId = query.value(11).toString();
    return record;
}

constexpr auto kEpisodeColumns =
    "number, title, image_url, published_at, is_new, state, "
    "position_ms, duration_ms, state_at, file_path, file_size, source_episode_id";

QString columnFor(Flag flag) {
    switch (flag) {
    case Flag::Favorite:
        return QStringLiteral("favorite");
    case Flag::Notify:
        return QStringLiteral("notify");
    case Flag::AutoDownload:
        return QStringLiteral("auto_download");
    }
    return {};
}

// Seasons, films and side stories of the same work. Spin-offs, summaries and alternative versions
// are works of their own: they never join a franchise.
bool isInstallment(api::RelationKind kind) {
    using api::RelationKind;
    switch (kind) {
    case RelationKind::Prequel:
    case RelationKind::Sequel:
    case RelationKind::SideStory:
    case RelationKind::MainStory:
    case RelationKind::FullStory:
        return true;
    case RelationKind::SpinOff:
    case RelationKind::Summary:
    case RelationKind::Alternative:
    case RelationKind::AlternativeSetting:
    case RelationKind::Other:
        break;
    }
    return false;
}

} // namespace

LibraryRepository::LibraryRepository(storage::Database& database) : database_(database) {}

Stored<void> LibraryRepository::saveSnapshot(const AnimeRecord& s) {
    const auto result =
        database_.run(QStringLiteral(R"sql(
INSERT INTO anime (slug, title, alternative_title, synopsis, poster_url, backdrop_url, category,
                   status, episode_count, genres, start_year, refreshed_at)
VALUES (?, ?, COALESCE(?, ''), COALESCE(?, ''), COALESCE(?, ''), COALESCE(?, ''),
        COALESCE(?, ''), ?, ?, COALESCE(?, ''), ?, ?)
ON CONFLICT(slug) DO UPDATE SET
    title = excluded.title,
    alternative_title = COALESCE(NULLIF(excluded.alternative_title, ''), alternative_title),
    synopsis = COALESCE(NULLIF(excluded.synopsis, ''), synopsis),
    poster_url = COALESCE(NULLIF(excluded.poster_url, ''), poster_url),
    backdrop_url = COALESCE(NULLIF(excluded.backdrop_url, ''), backdrop_url),
    category = COALESCE(NULLIF(excluded.category, ''), category),
    status = excluded.status,
    episode_count = COALESCE(excluded.episode_count, episode_count),
    genres = COALESCE(NULLIF(excluded.genres, ''), genres),
    start_year = COALESCE(excluded.start_year, start_year),
    refreshed_at = excluded.refreshed_at
)sql"),
                      {s.slug, s.title, s.alternativeTitle, s.synopsis, s.posterUrl.toString(),
                       s.backdropUrl.toString(), s.category, api::statusKey(s.status),
                       nullable(s.episodeCount), s.genres, nullable(s.startYear), nowMs()});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::setInLibrary(const QString& slug, bool in_library) {
    if (in_library) {
        auto clear =
            database_.run(QStringLiteral("DELETE FROM personal_exclusion WHERE slug = ?"), {slug});
        if (!clear)
            return std::unexpected(clear.error());
    }
    const auto result =
        in_library
            ? database_.run(
                  QStringLiteral(
                      "UPDATE anime SET in_library = 1, archived = 0, added_at = COALESCE("
                      "CASE WHEN in_library = 1 THEN added_at END, ?) "
                      "WHERE slug = ?"),
                  {nowMs(), slug})
            : database_.run(QStringLiteral("UPDATE anime SET in_library = 0, auto_download = 0, "
                                           "added_at = NULL WHERE slug = ?"),
                            {slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->numRowsAffected() == 0) {
        return std::unexpected(StorageError{QStringLiteral("unknown anime %1").arg(slug)});
    }
    return {};
}

Stored<void> LibraryRepository::setFlag(const QString& slug, Flag flag, bool value) {
    const auto result =
        database_.run(QStringLiteral("UPDATE anime SET %1 = ? WHERE slug = ?").arg(columnFor(flag)),
                      {value ? 1 : 0, slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->numRowsAffected() == 0) {
        return std::unexpected(StorageError{QStringLiteral("unknown anime %1").arg(slug)});
    }
    return {};
}

Stored<std::optional<AnimeRecord>> LibraryRepository::anime(const QString& slug) const {
    auto query = database_.run(
        QStringLiteral("SELECT %1 FROM anime WHERE slug = ?").arg(QLatin1StringView(kAnimeColumns)),
        {slug});
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        return std::optional<AnimeRecord>();
    }
    return std::optional<AnimeRecord>(readAnime(*query));
}

Stored<LibraryEntry> LibraryRepository::withProgress(LibraryEntry entry) const {
    auto counts = database_.run(QStringLiteral(R"sql(
SELECT COUNT(*),
       COALESCE(SUM(state = 2), 0),
       COALESCE(SUM(EXISTS(SELECT 1 FROM episode_file f WHERE f.slug = episode.slug AND f.number = episode.number)), 0),
       COALESCE(SUM(is_new), 0),
       MAX(CASE WHEN state = 2 THEN number END),
       (SELECT COALESCE(SUM(size), 0) FROM media_file WHERE id IN
           (SELECT file_id FROM episode_file WHERE slug = ?))
FROM episode WHERE slug = ?
)sql"),
                                {entry.slug, entry.slug});
    if (!counts) {
        return std::unexpected(counts.error());
    }
    counts->next();
    entry.knownEpisodes = counts->value(0).toInt();
    entry.watchedEpisodes = counts->value(1).toInt();
    entry.downloadedEpisodes = counts->value(2).toInt();
    entry.newEpisodes = counts->value(3).toInt();
    const auto last_watched = counts->value(4);
    entry.downloadedBytes = counts->value(5).toLongLong();

    auto watching = database_.run(QStringLiteral("SELECT number, position_ms, duration_ms FROM "
                                                 "episode WHERE slug = ? AND state = 1 "
                                                 "ORDER BY state_at DESC LIMIT 1"),
                                  {entry.slug});
    if (!watching) {
        return std::unexpected(watching.error());
    }
    if (watching->next()) {
        entry.resumeNumber = watching->value(0).toDouble();
        entry.resumePositionMs = watching->value(1).toLongLong();
        entry.resumeDurationMs = watching->value(2).toLongLong();
    } else {
        auto next =
            database_.run(QStringLiteral("SELECT MIN(number) FROM episode WHERE slug = ? "
                                         "AND state = 0 AND number > ?"),
                          {entry.slug, last_watched.isNull() ? -1.0 : last_watched.toDouble()});
        if (!next) {
            return std::unexpected(next.error());
        }
        if (next->next() && !next->value(0).isNull()) {
            entry.resumeNumber = next->value(0).toDouble();
        } else if (!entry.finished()) {
            // Nothing past the last watched episode, yet the series is not seen: it goes on at
            // the first whole episode skipped. A recap like 17.5 is not one.
            auto skipped = database_.run(QStringLiteral("SELECT MIN(number) FROM episode WHERE "
                                                        "slug = ? AND state = 0 AND number = "
                                                        "CAST(number AS INTEGER)"),
                                         {entry.slug});
            if (!skipped) {
                return std::unexpected(skipped.error());
            }
            if (skipped->next() && !skipped->value(0).isNull()) {
                entry.resumeNumber = skipped->value(0).toDouble();
            }
        }
    }
    if (!entry.resumeNumber) {
        return entry;
    }
    auto ahead = database_.run(QStringLiteral(R"sql(
SELECT number,
       EXISTS(SELECT 1 FROM episode_file f WHERE f.slug = episode.slug AND f.number = episode.number)
FROM episode WHERE slug = ? AND state <> 2 AND number >= ? ORDER BY number
)sql"),
                               {entry.slug, *entry.resumeNumber});
    if (!ahead) {
        return std::unexpected(ahead.error());
    }
    while (ahead->next()) {
        const auto number = ahead->value(0).toDouble();
        if (ahead->value(1).toBool()) {
            entry.resumeOnDisk = entry.resumeOnDisk || number == *entry.resumeNumber;
            ++entry.unwatchedOnDisk;
        } else {
            entry.missingEpisodes.append(number);
        }
    }
    return entry;
}

Stored<QList<LibraryEntry>> LibraryRepository::entries(bool include_archived) const {
    return queryEntries(include_archived ? QStringLiteral("in_library = 1")
                                         : QStringLiteral("in_library = 1 AND archived = 0"));
}

Stored<QList<LibraryEntry>> LibraryRepository::followedEntries() const {
    return queryEntries(QStringLiteral("in_library = 1 AND (notify = 1 OR auto_download = 1)"));
}

Stored<QList<LibraryEntry>> LibraryRepository::queryEntries(const QString& predicate) const {
    auto query = database_.run(QStringLiteral("SELECT %1 FROM anime WHERE %2 "
                                              "ORDER BY title COLLATE NOCASE")
                                   .arg(QLatin1StringView(kAnimeColumns), predicate));
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<LibraryEntry> entries;
    while (query->next()) {
        LibraryEntry entry;
        static_cast<AnimeRecord&>(entry) = readAnime(*query);
        auto complete = withProgress(std::move(entry));
        if (!complete) {
            return std::unexpected(complete.error());
        }
        entries.append(std::move(*complete));
    }
    return entries;
}

Stored<std::optional<LibraryEntry>> LibraryRepository::entry(const QString& slug) const {
    auto record = anime(slug);
    if (!record) {
        return std::unexpected(record.error());
    }
    if (!*record) {
        return std::optional<LibraryEntry>();
    }
    LibraryEntry entry;
    static_cast<AnimeRecord&>(entry) = **record;
    auto complete = withProgress(std::move(entry));
    if (!complete) {
        return std::unexpected(complete.error());
    }
    return std::optional<LibraryEntry>(std::move(*complete));
}

Stored<QList<double>> LibraryRepository::syncEpisodes(const QString& slug,
                                                      const QList<api::Episode>& episodes,
                                                      bool flag_new) {
    QList<double> added;
    auto stored = database_.transaction([&]() -> Stored<void> {
        const auto now = nowMs();
        for (const auto& episode : episodes) {
            auto exists =
                database_.run(QStringLiteral("SELECT 1 FROM episode WHERE slug = ? AND number = ?"),
                              {slug, episode.number});
            if (!exists) {
                return std::unexpected(exists.error());
            }
            const bool known = exists->next();
            exists->finish();
            auto written = database_.run(
                QStringLiteral(R"sql(
INSERT INTO episode (slug, number, title, image_url, published_at, known_at, is_new, source_episode_id)
VALUES (?, ?, COALESCE(?, ''), COALESCE(?, ''), ?, ?, ?, COALESCE(?, ''))
ON CONFLICT(slug, number) DO UPDATE SET
    title = excluded.title,
    image_url = excluded.image_url,
    published_at = COALESCE(excluded.published_at, published_at),
    source_episode_id = COALESCE(NULLIF(excluded.source_episode_id, ''), source_episode_id)
)sql"),
                {slug, episode.number, episode.title, episode.imageUrl.toString(),
                 nullable(episode.publishedAt), now, flag_new && !known ? 1 : 0, episode.id});
            if (!written) {
                return std::unexpected(written.error());
            }
            if (!known) {
                added.append(episode.number);
            }
        }
        auto synced = database_.run(
            QStringLiteral("UPDATE anime SET episodes_synced_at = ? WHERE slug = ?"), {now, slug});
        if (!synced) {
            return std::unexpected(synced.error());
        }
        return {};
    });
    if (!stored) {
        return std::unexpected(stored.error());
    }
    return added;
}

Stored<QList<EpisodeRecord>> LibraryRepository::episodes(const QString& slug) const {
    auto query = database_.run(QStringLiteral("SELECT %1 FROM episode WHERE slug = ? ORDER BY "
                                              "number")
                                   .arg(QLatin1StringView(kEpisodeColumns)),
                               {slug});
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<EpisodeRecord> episodes;
    while (query->next()) {
        auto record = readEpisode(*query);
        if (auto best = selectBestFile(slug, record); !best) {
            return std::unexpected(best.error());
        }
        episodes.append(std::move(record));
    }
    return episodes;
}

Stored<std::optional<EpisodeRecord>> LibraryRepository::episode(const QString& slug,
                                                                double number) const {
    auto query = database_.run(QStringLiteral("SELECT %1 FROM episode WHERE slug = ? AND "
                                              "number = ?")
                                   .arg(QLatin1StringView(kEpisodeColumns)),
                               {slug, number});
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        return std::optional<EpisodeRecord>();
    }
    auto record = readEpisode(*query);
    if (auto best = selectBestFile(slug, record); !best) {
        return std::unexpected(best.error());
    }
    return std::optional<EpisodeRecord>(std::move(record));
}

Stored<void> LibraryRepository::ensureEpisode(const QString& slug, double number) {
    auto result = database_.run(QStringLiteral("INSERT OR IGNORE INTO episode (slug, number, "
                                               "known_at) VALUES (?, ?, ?)"),
                                {slug, number, nowMs()});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::touch(const QString& slug) {
    auto result = database_.run(
        QStringLiteral("UPDATE anime SET last_activity_at = ? WHERE slug = ?"), {nowMs(), slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::setWatchState(const QString& slug, double number, WatchState state,
                                              qint64 position_ms, qint64 duration_ms) {
    return database_.transaction([&]() -> Stored<void> {
        if (auto ensured = ensureEpisode(slug, number); !ensured) {
            return ensured;
        }
        const bool watching = state == WatchState::Watching;
        auto result = database_.run(
            QStringLiteral("UPDATE episode SET state = ?, position_ms = ?, duration_ms = "
                           "CASE WHEN ? > 0 THEN ? ELSE duration_ms END, state_at = ?, is_new = 0 "
                           "WHERE slug = ? AND number = ?"),
            {static_cast<int>(state), watching ? std::max<qint64>(position_ms, 0) : 0, duration_ms,
             duration_ms, nowMs(), slug, number});
        if (!result) {
            return std::unexpected(result.error());
        }
        if (state == WatchState::Watching) {
            // Only one episode of an anime is "in progress" at a time.
            auto others = database_.run(
                QStringLiteral("UPDATE episode SET state = 0, position_ms = 0 WHERE slug = ? "
                               "AND state = 1 AND number <> ?"),
                {slug, number});
            if (!others) {
                return std::unexpected(others.error());
            }
        }
        return touch(slug);
    });
}

Stored<void> LibraryRepository::setWatchedThrough(const QString& slug, double number) {
    return database_.transaction([&]() -> Stored<void> {
        if (auto ensured = ensureEpisode(slug, number); !ensured) {
            return ensured;
        }
        auto result = database_.run(
            QStringLiteral("UPDATE episode SET state = 2, position_ms = 0, state_at = ?, "
                           "is_new = 0 WHERE slug = ? AND number <= ? AND state <> 2"),
            {nowMs(), slug, number});
        if (!result) {
            return std::unexpected(result.error());
        }
        return touch(slug);
    });
}

Stored<void> LibraryRepository::setAllUnwatched(const QString& slug) {
    auto result = database_.run(QStringLiteral("UPDATE episode SET state = 0, position_ms = 0, "
                                               "state_at = ? WHERE slug = ?"),
                                {nowMs(), slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::setEpisodeFile(const QString& slug, double number,
                                               const QString& path, qint64 size) {
    return database_.transaction(
        [&]() -> Stored<void> { return storeEpisodeFile(slug, number, path, size); });
}

Stored<void> LibraryRepository::clearEpisodeFile(const QString& slug, double number) {
    return database_.transaction([&]() -> Stored<void> {
        const auto locations = episodeLocations(slug, number);
        if (!locations)
            return std::unexpected(locations.error());
        auto result =
            database_.run(QStringLiteral("DELETE FROM episode_file WHERE slug = ? AND number = ?"),
                          {slug, number});
        if (!result)
            return std::unexpected(result.error());
        for (const auto& location : *locations) {
            if (auto pruned = pruneUnreferencedFile(location.path); !pruned)
                return pruned;
        }
        return updateLegacyFile(slug, number);
    });
}

Stored<void> LibraryRepository::clearNew(const QString& slug) {
    auto result = database_.run(
        QStringLiteral("UPDATE episode SET is_new = 0 WHERE slug = ? AND is_new = 1"), {slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<QList<std::tuple<QString, double, QString>>> LibraryRepository::episodeFiles() const {
    auto query = database_.run(QStringLiteral("SELECT e.slug, e.number, f.path FROM episode_file e "
                                              "JOIN media_file f ON f.id = e.file_id"));
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<std::tuple<QString, double, QString>> files;
    while (query->next()) {
        files.append(
            {query->value(0).toString(), query->value(1).toDouble(), query->value(2).toString()});
    }
    return files;
}

namespace {
QString normalizedPath(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
QString pathKey(const QString& path) {
    return normalizedPath(path).toCaseFolded();
}
} // namespace

Stored<void> LibraryRepository::setArchived(const QString& slug, bool archived) {
    auto result = database_.run(
        QStringLiteral("UPDATE anime SET archived = ? WHERE slug = ? AND in_library = 1"),
        {archived ? 1 : 0, slug});
    if (!result)
        return std::unexpected(result.error());
    if (!result->numRowsAffected())
        return std::unexpected(StorageError{QStringLiteral("unknown personal anime")});
    return {};
}

Stored<void> LibraryRepository::removePersonalRecord(const QString& slug) {
    return database_.transaction([&]() -> Stored<void> {
        auto excluded =
            database_.run(QStringLiteral("INSERT OR IGNORE INTO personal_exclusion(slug) SELECT "
                                         "slug FROM anime WHERE slug = ?"),
                          {slug});
        if (!excluded)
            return std::unexpected(excluded.error());
        for (const auto& sql :
             {QStringLiteral(
                  "DELETE FROM franchise WHERE EXISTS (SELECT 1 FROM franchise_member target WHERE "
                  "target.franchise_id = franchise.id AND target.slug = ? AND NOT EXISTS (SELECT 1 "
                  "FROM franchise_member other WHERE other.franchise_id = franchise.id AND "
                  "other.included = 1 AND other.slug <> target.slug))"),
              QStringLiteral("DELETE FROM franchise_member WHERE slug = ?"),
              QStringLiteral("DELETE FROM episode_file WHERE slug = ?"),
              QStringLiteral(
                  "UPDATE episode SET state = 0, position_ms = 0, duration_ms = 0, state_at = "
                  "NULL, is_new = 0, file_path = NULL, file_size = NULL WHERE slug = ?"),
              // Back to an anime never added: added again, it notifies like any other.
              QStringLiteral(
                  "UPDATE anime SET in_library = 0, archived = 0, favorite = 0, notify = 1, "
                  "auto_download = 0, added_at = NULL, last_activity_at = NULL WHERE slug = ?")}) {
            auto result = database_.run(sql, {slug});
            if (!result)
                return std::unexpected(result.error());
        }
        auto cleaned =
            database_.run(QStringLiteral("DELETE FROM media_file WHERE NOT EXISTS (SELECT 1 FROM "
                                         "episode_file WHERE file_id = media_file.id)"));
        if (!cleaned)
            return std::unexpected(cleaned.error());
        return {};
    });
}

Stored<qint64> LibraryRepository::createFranchise(const QString& title) {
    if (title.trimmed().isEmpty())
        return std::unexpected(StorageError{QStringLiteral("empty franchise title")});
    auto result =
        database_.run(QStringLiteral("INSERT INTO franchise(title) VALUES (?)"), {title.trimmed()});
    if (!result)
        return std::unexpected(result.error());
    return result->lastInsertId().toLongLong();
}

Stored<QList<FranchiseRecord>> LibraryRepository::franchises() const {
    auto result = database_.run(
        QStringLiteral("SELECT id, title FROM franchise ORDER BY title COLLATE NOCASE, id"));
    if (!result)
        return std::unexpected(result.error());
    QList<FranchiseRecord> records;
    while (result->next()) {
        FranchiseRecord record{.id = result->value(0).toLongLong(),
                               .title = result->value(1).toString()};
        auto members =
            database_.run(QStringLiteral("SELECT slug FROM franchise_member WHERE franchise_id = ? "
                                         "AND included = 1 ORDER BY slug"),
                          {record.id});
        if (!members)
            return std::unexpected(members.error());
        while (members->next())
            record.members.append(members->value(0).toString());
        records.append(std::move(record));
    }
    return records;
}

Stored<void> LibraryRepository::setFranchiseMember(qint64 id, const QString& slug, bool included) {
    return database_.transaction([&]() { return writeFranchiseMember(id, slug, included); });
}

Stored<void> LibraryRepository::writeFranchiseMember(qint64 id, const QString& slug,
                                                     bool included) {
    if (included) {
        auto clear =
            database_.run(QStringLiteral("DELETE FROM personal_exclusion WHERE slug = ?"), {slug});
        if (!clear)
            return std::unexpected(clear.error());
        auto exclude =
            database_.run(QStringLiteral("UPDATE franchise_member SET included = 0, manual = 1 "
                                         "WHERE slug = ? AND franchise_id <> ?"),
                          {slug, id});
        if (!exclude)
            return std::unexpected(exclude.error());
    }
    auto result = database_.run(
        QStringLiteral("INSERT INTO franchise_member(franchise_id, slug, included, manual) "
                       "VALUES (?, ?, ?, 1) ON CONFLICT(franchise_id, slug) DO UPDATE SET "
                       "included = excluded.included, manual = 1"),
        {id, slug, included ? 1 : 0});
    if (!result)
        return std::unexpected(result.error());
    return {};
}

Stored<qint64> LibraryRepository::createFranchise(const QString& title,
                                                  const QString& initial_slug) {
    qint64 id = 0;
    auto result = database_.transaction([&]() -> Stored<void> {
        auto created = createFranchise(title);
        if (!created)
            return std::unexpected(created.error());
        id = *created;
        return writeFranchiseMember(id, initial_slug, true);
    });
    if (!result)
        return std::unexpected(result.error());
    return id;
}

Stored<QStringList> LibraryRepository::discoverFranchiseRelations(const api::AnimeDetail& detail) {
    QStringList added;
    const auto applied = database_.transaction([&]() -> Stored<void> {
        if (auto snapshot = saveSnapshot(snapshotOf(detail)); !snapshot)
            return snapshot;
        // Only the relations of an anime the user follows reach the library.
        auto followed = database_.run(
            QStringLiteral("SELECT 1 FROM anime WHERE slug = ? AND in_library = 1 AND "
                           "(notify = 1 OR auto_download = 1)"),
            {detail.slug});
        if (!followed)
            return std::unexpected(followed.error());
        if (!followed->next())
            return {};
        followed->finish();
        const auto neighborsSql =
            QStringLiteral("SELECT target_slug FROM anime_relation WHERE source_slug = ? UNION "
                           "SELECT source_slug FROM anime_relation WHERE target_slug = ?");
        QStringList affected{detail.slug};
        const auto neighbors = [&]() -> Stored<void> {
            auto query = database_.run(neighborsSql, {detail.slug, detail.slug});
            if (!query)
                return std::unexpected(query.error());
            while (query->next())
                affected.append(query->value(0).toString());
            return {};
        };
        // Neighbors it no longer links to are evaluated again too: they may now belong to just
        // one group.
        if (auto former = neighbors(); !former)
            return former;
        // The detail lists every link it starts and the kind of each one it receives; only
        // installments are kept.
        auto outgoing = database_.run(
            QStringLiteral("DELETE FROM anime_relation WHERE source_slug = ?"), {detail.slug});
        if (!outgoing)
            return std::unexpected(outgoing.error());
        for (const auto& relation : detail.incomingRelations) {
            auto incoming = database_.run(
                QStringLiteral(
                    "DELETE FROM anime_relation WHERE source_slug = ? AND target_slug = ?"),
                {relation.anime.slug, detail.slug});
            if (!incoming)
                return std::unexpected(incoming.error());
        }
        const auto link = [&](const api::Relation& relation, bool incoming) -> Stored<void> {
            if (relation.anime.slug.isEmpty() || relation.anime.slug == detail.slug ||
                !isInstallment(relation.kind))
                return {};
            if (auto saved = saveSnapshot(snapshotOf(relation.anime)); !saved)
                return saved;
            auto edge = database_.run(
                QStringLiteral(
                    "INSERT OR IGNORE INTO anime_relation(source_slug, target_slug) VALUES (?, ?)"),
                {incoming ? relation.anime.slug : detail.slug,
                 incoming ? detail.slug : relation.anime.slug});
            if (!edge)
                return std::unexpected(edge.error());
            return {};
        };
        for (const auto& relation : detail.relations) {
            if (auto saved = link(relation, false); !saved)
                return saved;
        }
        for (const auto& relation : detail.incomingRelations) {
            if (auto saved = link(relation, true); !saved)
                return saved;
        }
        if (auto current = neighbors(); !current)
            return current;
        affected.removeDuplicates();
        struct Admission {
            QString slug;
            qint64 group;
            bool archived;
            bool notify;
        };
        QList<Admission> admissions;
        // Evaluate every work before admitting any: iteration order cannot change the outcome.
        // A work related to several groups, or to none, stays as it is.
        for (const auto& slug : affected) {
            auto excluded = database_.run(
                QStringLiteral(
                    "SELECT 1 FROM personal_exclusion WHERE slug = ? UNION ALL SELECT 1 FROM "
                    "franchise_member WHERE slug = ? AND manual = 1 AND included = 0 LIMIT 1"),
                {slug, slug});
            if (!excluded)
                return std::unexpected(excluded.error());
            if (excluded->next())
                continue;
            auto owner = database_.run(
                QStringLiteral("SELECT 1 FROM franchise_member WHERE slug = ? AND included = 1"),
                {slug});
            if (!owner)
                return std::unexpected(owner.error());
            if (owner->next())
                continue;
            auto groups = database_.run(
                QStringLiteral("SELECT DISTINCT m.franchise_id FROM franchise_member m JOIN anime "
                               "a ON a.slug = m.slug WHERE m.included = 1 AND a.in_library = 1 AND "
                               "a.notify = 1 AND m.slug IN (%1)")
                    .arg(neighborsSql),
                {slug, slug});
            if (!groups)
                return std::unexpected(groups.error());
            QList<qint64> ids;
            while (groups->next())
                ids.append(groups->value(0).toLongLong());
            if (ids.size() != 1)
                continue;
            // A second un-followed personal group is still a conflict, never a merge.
            auto allGroups = database_.run(
                QStringLiteral("SELECT COUNT(DISTINCT m.franchise_id) FROM franchise_member m "
                               "JOIN anime a ON a.slug = m.slug WHERE m.included = 1 AND "
                               "a.in_library = 1 AND m.slug IN (%1)")
                    .arg(neighborsSql),
                {slug, slug});
            if (!allGroups)
                return std::unexpected(allGroups.error());
            allGroups->next();
            if (allGroups->value(0).toInt() != 1)
                continue;
            auto continuity = database_.run(
                QStringLiteral("SELECT MIN(a.archived), MAX(a.notify) FROM anime a JOIN "
                               "franchise_member m ON m.slug = a.slug WHERE m.franchise_id "
                               "= ? AND m.included = 1 AND a.in_library = 1"),
                {ids.first()});
            if (!continuity)
                return std::unexpected(continuity.error());
            continuity->next();
            admissions.append(
                {slug, ids.first(), continuity->value(0).toBool(), continuity->value(1).toBool()});
        }
        for (const auto& admission : admissions) {
            auto member = database_.run(
                QStringLiteral("INSERT INTO franchise_member(franchise_id, slug, included, manual) "
                               "VALUES (?, ?, 1, 0) ON CONFLICT(franchise_id, slug) DO UPDATE SET "
                               "included = 1 WHERE manual = 0"),
                {admission.group, admission.slug});
            if (!member)
                return std::unexpected(member.error());
            auto personal = database_.run(
                QStringLiteral(
                    "UPDATE anime SET archived = CASE WHEN in_library = 1 THEN archived ELSE ? "
                    "END, notify = CASE WHEN in_library = 1 THEN notify ELSE ? END, added_at = "
                    "COALESCE(added_at, ?), in_library = 1 WHERE slug = ?"),
                {admission.archived ? 1 : 0, admission.notify ? 1 : 0, nowMs(), admission.slug});
            if (!personal)
                return std::unexpected(personal.error());
            added.append(admission.slug);
        }
        return {};
    });
    if (!applied)
        return std::unexpected(applied.error());
    return added;
}
Stored<void> LibraryRepository::storeEpisodeFile(const QString& slug, double number,
                                                 const QString& path, qint64 size) {
    if (path.trimmed().isEmpty() || !QFileInfo(path).isAbsolute() || size < 0)
        return std::unexpected(StorageError{QStringLiteral("invalid media path or size")});
    if (auto ensured = ensureEpisode(slug, number); !ensured)
        return ensured;
    auto file =
        database_.run(QStringLiteral("INSERT INTO media_file(path, path_key, size) VALUES (?, ?, "
                                     "?) ON CONFLICT(path_key) DO UPDATE SET size = excluded.size"),
                      {normalizedPath(path), pathKey(path), size});
    if (!file)
        return std::unexpected(file.error());
    auto mapping =
        database_.run(QStringLiteral("INSERT OR IGNORE INTO episode_file(slug, number, file_id) "
                                     "SELECT ?, ?, id FROM media_file WHERE path_key = ?"),
                      {slug, number, pathKey(path)});
    if (!mapping)
        return std::unexpected(mapping.error());
    return updateLegacyFile(slug, number);
}

Stored<QList<EpisodeLocation>> LibraryRepository::episodeLocations(const QString& slug,
                                                                   double number) const {
    auto query = database_.run(
        QStringLiteral("SELECT f.path, f.size FROM episode_file e JOIN media_file f ON f.id = "
                       "e.file_id WHERE e.slug = ? AND e.number = ? ORDER BY f.id"),
        {slug, number});
    if (!query)
        return std::unexpected(query.error());
    QList<EpisodeLocation> result;
    while (query->next())
        result.append({query->value(0).toString(), query->value(1).toLongLong()});
    return result;
}

Stored<void> LibraryRepository::selectBestFile(const QString& slug, EpisodeRecord& episode) const {
    auto locations = episodeLocations(slug, episode.number);
    if (!locations)
        return std::unexpected(locations.error());
    episode.filePath.clear();
    episode.fileSize = 0;
    for (const auto& location : *locations) {
        if (episode.filePath.isEmpty() || QFileInfo::exists(location.path)) {
            episode.filePath = location.path;
            episode.fileSize = location.size;
            if (QFileInfo::exists(location.path))
                break;
        }
    }
    return {};
}

Stored<void> LibraryRepository::updateLegacyFile(const QString& slug, double number) {
    EpisodeRecord record;
    record.number = number;
    if (auto best = selectBestFile(slug, record); !best)
        return best;
    auto result = database_.run(
        QStringLiteral(
            "UPDATE episode SET file_path = ?, file_size = ? WHERE slug = ? AND number = ?"),
        {record.filePath.isEmpty() ? QVariant(QMetaType::fromType<QString>())
                                   : QVariant(record.filePath),
         record.filePath.isEmpty() ? QVariant(QMetaType::fromType<qint64>())
                                   : QVariant(record.fileSize),
         slug, number});
    if (!result)
        return std::unexpected(result.error());
    return {};
}

Stored<void> LibraryRepository::pruneUnreferencedFile(const QString& path) {
    auto cleanup =
        database_.run(QStringLiteral("DELETE FROM media_file WHERE path_key = ? AND NOT EXISTS "
                                     "(SELECT 1 FROM episode_file WHERE file_id = media_file.id)"),
                      {pathKey(path)});
    if (!cleanup)
        return std::unexpected(cleanup.error());
    return {};
}

Stored<QList<std::tuple<QString, double>>>
LibraryRepository::episodeFileMappings(const QString& path) const {
    auto query = database_.run(
        QStringLiteral("SELECT e.slug, e.number FROM episode_file e JOIN media_file f ON f.id = "
                       "e.file_id WHERE f.path_key = ? ORDER BY e.slug, e.number"),
        {pathKey(path)});
    if (!query)
        return std::unexpected(query.error());
    QList<std::tuple<QString, double>> result;
    while (query->next())
        result.append({query->value(0).toString(), query->value(1).toDouble()});
    return result;
}

Stored<void> LibraryRepository::removeEpisodeFile(const QString& slug, double number,
                                                  const QString& path) {
    return database_.transaction([&]() -> Stored<void> {
        auto result = database_.run(
            QStringLiteral("DELETE FROM episode_file WHERE slug = ? AND number = ? AND file_id IN "
                           "(SELECT id FROM media_file WHERE path_key = ?)"),
            {slug, number, pathKey(path)});
        if (!result)
            return std::unexpected(result.error());
        if (auto pruned = pruneUnreferencedFile(path); !pruned)
            return pruned;
        return updateLegacyFile(slug, number);
    });
}

Stored<void> LibraryRepository::importEpisodeFile(const QString& slug, double number,
                                                  const QString& path, qint64 size, bool watched) {
    return database_.transaction([&]() -> Stored<void> {
        if (auto stored = storeEpisodeFile(slug, number, path, size); !stored)
            return stored;
        if (watched) {
            auto state = database_.run(
                QStringLiteral("UPDATE episode SET state = 2, state_at = NULL, position_ms = 0, "
                               "is_new = 0 WHERE slug = ? AND number = ? AND state = 0"),
                {slug, number});
            if (!state)
                return std::unexpected(state.error());
        }
        return {};
    });
}

} // namespace animehub::library
