#include "work_folders.h"

#include "downloads/download_repository.h"
#include "library_repository.h"
#include "util/file_names.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <optional>

namespace animehub::library {
namespace {

using storage::StorageError;

constexpr auto kMarker = "work-folders-by-title";

struct WorkPath {
    QString slug;
    QString path;
    bool unfinished = false;
    bool handedOver = false; // JDownloader may be writing in its folder
};

// Every path the library and the download queue store, with the work it belongs to.
Stored<QList<WorkPath>> workPaths(const storage::Database& database) {
    QList<WorkPath> paths;
    auto files = database.run(QStringLiteral(
        "SELECT e.slug, f.path FROM episode_file e JOIN media_file f ON f.id = e.file_id"));
    if (!files)
        return std::unexpected(files.error());
    while (files->next())
        paths.append({files->value(0).toString(), files->value(1).toString()});
    auto tasks = database.run(QStringLiteral(
        "SELECT slug, target_path, engine_destination, state NOT IN ('completed', 'cancelled'), "
        "engine <> 'native' AND engine_submission <> '' FROM download"));
    if (!tasks)
        return std::unexpected(tasks.error());
    while (tasks->next()) {
        const bool unfinished = tasks->value(3).toBool();
        const bool handed = unfinished && tasks->value(4).toBool();
        paths.append({tasks->value(0).toString(), tasks->value(1).toString(), unfinished, handed});
        if (!tasks->value(2).toString().isEmpty())
            paths.append(
                {tasks->value(0).toString(), tasks->value(2).toString(), unfinished, handed});
    }
    return paths;
}

QString normalized(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

// The folder directly under `root` that holds `path`; empty when `path` is not below one.
QString folderUnder(const QString& root, const QString& path) {
    const auto clean = normalized(path);
    if (clean.size() <= root.size() + 1 ||
        !clean.startsWith(root + u'/', Qt::CaseInsensitive)) // as util::isInside
        return {};
    const auto end = clean.indexOf(u'/', root.size() + 1);
    return end < 0 ? QString() : clean.left(end);
}

QString keyOf(const QString& folder) {
    return folder.toCaseFolded();
}

Stored<std::optional<int>> startYear(const storage::Database& database, const QString& slug) {
    auto query =
        database.run(QStringLiteral("SELECT start_year FROM anime WHERE slug = ?"), {slug});
    if (!query)
        return std::unexpected(query.error());
    if (!query->next() || query->value(0).isNull())
        return std::optional<int>();
    return std::optional<int>(query->value(0).toInt());
}

// The first name of the work that no other work uses and, with `on_disk`, no folder has.
QString freeFolder(const QString& root, const QString& slug, const QString& title,
                   std::optional<int> year, const QSet<QString>& others, bool on_disk) {
    QString folder;
    for (int attempt = 0; attempt < 3; ++attempt) {
        folder = root + u'/' + util::workFolderName(title, year, slug, attempt);
        if (!others.contains(keyOf(folder)) && !(on_disk && QFileInfo::exists(folder)))
            break;
    }
    return folder;
}

} // namespace

Stored<QString> workFolder(const storage::Database& database, const QString& directory,
                           const QString& slug, const QString& title) {
    const auto root = normalized(directory);
    const auto paths = workPaths(database);
    if (!paths)
        return std::unexpected(paths.error());
    struct Own {
        QString folder;
        int count = 0;
    };
    QHash<QString, Own> own;
    QHash<QString, bool> exists;
    QSet<QString> others;
    for (const auto& entry : *paths) {
        const auto folder = folderUnder(root, entry.path);
        if (folder.isEmpty())
            continue;
        const auto key = keyOf(folder);
        if (entry.slug != slug) {
            others.insert(key);
            continue;
        }
        // A folder gone from disk with nothing on its way to it is no longer the work's.
        const auto found = exists.find(key);
        const bool present =
            found != exists.end() ? *found : *exists.insert(key, QFileInfo(folder).isDir());
        if (present || entry.unfinished) {
            auto& counted = own[key];
            counted.folder = folder;
            ++counted.count;
        }
    }
    if (!own.isEmpty()) {
        const auto best = std::ranges::max_element(own, [](const Own& a, const Own& b) {
            return a.count != b.count ? a.count < b.count
                                      : a.folder.compare(b.folder, Qt::CaseInsensitive) > 0;
        });
        return best->folder;
    }
    const auto year = startYear(database, slug);
    if (!year)
        return std::unexpected(year.error());
    return freeFolder(root, slug, title, *year, others, true);
}

Stored<FolderMigration> migrateWorkFolders(storage::Database& database, const QString& directory) {
    FolderMigration result;
    auto marker = database.run(QStringLiteral("SELECT 1 FROM maintenance WHERE name = ?"),
                               {QString::fromLatin1(kMarker)});
    if (!marker)
        return std::unexpected(marker.error());
    if (marker->next()) {
        result.done = true;
        return result;
    }
    marker->finish();
    const auto root = normalized(directory);
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return result; // an unplugged drive: tried again later

    const auto paths = workPaths(database);
    if (!paths)
        return std::unexpected(paths.error());
    struct Folder {
        QString path;
        QSet<QString> slugs;
        bool handedOver = false;
    };
    QHash<QString, Folder> folders;
    for (const auto& entry : *paths) {
        const auto folder = folderUnder(root, entry.path);
        if (folder.isEmpty())
            continue;
        auto& known = folders[keyOf(folder)];
        known.path = folder;
        known.slugs.insert(entry.slug);
        known.handedOver = known.handedOver || entry.handedOver;
    }

    struct Work {
        QString title;
        std::optional<int> year;
    };
    QHash<QString, Work> works;
    auto anime = database.run(QStringLiteral("SELECT slug, title, start_year FROM anime"));
    if (!anime)
        return std::unexpected(anime.error());
    while (anime->next())
        works.insert(anime->value(0).toString(),
                     {anime->value(1).toString(), anime->value(2).isNull()
                                                      ? std::nullopt
                                                      : std::optional(anime->value(2).toInt())});
    anime->finish();

    struct Candidate {
        QString key;
        QString slug;
    };
    QList<Candidate> candidates;
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        for (const auto& slug : it->slugs) {
            if (!works.contains(slug) ||
                QFileInfo(it->path).fileName() != util::legacyWorkFolderName(slug))
                continue;
            if (it->slugs.size() > 1) {
                result.skipped.append(
                    {slug, it->path, QStringLiteral("holds files of other works"), false});
            } else {
                candidates.append({it.key(), slug});
            }
        }
    }
    // The earliest work keeps the plain title when two share it.
    std::ranges::sort(candidates, [&works](const Candidate& a, const Candidate& b) {
        const auto& x = works[a.slug];
        const auto& y = works[b.slug];
        if (x.year.has_value() != y.year.has_value())
            return x.year.has_value();
        if (x.year != y.year)
            return *x.year < *y.year;
        return a.slug < b.slug;
    });

    LibraryRepository library(database);
    downloads::DownloadRepository queue(database);
    for (const auto& candidate : candidates) {
        const auto& source = folders[candidate.key];
        const auto& work = works[candidate.slug];
        const auto skip = [&](const QString& reason) {
            result.skipped.append({candidate.slug, source.path, reason, true});
        };
        if (source.handedOver) {
            skip(QStringLiteral("handed over to JDownloader"));
            continue;
        }
        QSet<QString> others;
        for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
            if (!(it->slugs.size() == 1 && it->slugs.contains(candidate.slug)))
                others.insert(it.key());
        }
        const auto target = freeFolder(root, candidate.slug, work.title, work.year, others, false);
        if (target == source.path)
            continue;
        const bool same_folder = keyOf(target) == candidate.key; // only the case changes
        if (!same_folder && QFileInfo::exists(target)) {
            skip(QStringLiteral("the new name exists"));
            continue;
        }
        const bool on_disk = QFileInfo(source.path).isDir();
        if (on_disk && !QDir().rename(source.path, target)) {
            skip(QStringLiteral("cannot rename (in use)"));
            continue;
        }
        FolderMove move{candidate.slug, source.path, target};
        auto updated = database.transaction([&]() -> Stored<void> {
            auto files = library.relocateFiles(source.path, target);
            if (!files)
                return std::unexpected(files.error());
            auto rows = queue.relocate(source.path, target);
            if (!rows)
                return std::unexpected(rows.error());
            move.files = *files;
            move.downloads = *rows;
            return {};
        });
        if (!updated) {
            if (on_disk && !QDir().rename(target, source.path)) {
                result.skipped.append(
                    {candidate.slug, target,
                     QStringLiteral("library not updated and folder not renamed back: ") +
                         updated.error().message,
                     false});
            } else {
                skip(QStringLiteral("library not updated: ") + updated.error().message);
            }
            continue;
        }
        result.moved.append(move);
        auto moved = folders.take(candidate.key);
        moved.path = target;
        folders.insert(keyOf(target), moved);
    }
    result.done = std::ranges::none_of(result.skipped, &FolderSkip::retry);
    if (result.done) {
        auto saved =
            database.run(QStringLiteral("INSERT INTO maintenance(name, done_at) VALUES (?, ?)"),
                         {QString::fromLatin1(kMarker), storage::nowMs()});
        if (!saved)
            return std::unexpected(saved.error());
    }
    return result;
}

} // namespace animehub::library
