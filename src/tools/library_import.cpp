#include "api/api_json.h"
#include "library/library_repository.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

#include <cmath>
#include <stdexcept>
#include <type_traits>

using namespace animehub;

namespace {
void require(bool condition, const QString& message) {
    if (!condition)
        throw std::runtime_error(message.toStdString());
}
template <class T> T checked(std::expected<T, storage::StorageError> result) {
    require(result.has_value(), result ? QString{} : result.error().message);
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}
QJsonObject readObject(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot read %1").arg(path));
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            QStringLiteral("Invalid JSON object: %1").arg(path));
    return document.object();
}
struct Catalog {
    api::AnimeDetail anime;
    QList<api::Episode> episodes;
};
QString normalized(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}
// Snapshot the closed destination without migrating it. All repository writes go to the
// disposable staging database. Live originals require a separate coherent SQLite backup.
void snapshot(const QString& source, const QString& destination) {
    const auto name = QUuid::createUuid().toString();
    QString error;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        // Caller has rejected WAL/SHM sidecars: this closed destination is immutable during
        // snapshotting. URI mode also prevents a read-only WAL connection creating sidecars.
        db.setDatabaseName(QUrl::fromLocalFile(source).toString() + QStringLiteral("?immutable=1"));
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_OPEN_URI"));
        if (!db.open()) {
            error = db.lastError().text();
        } else {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("VACUUM INTO ?"));
            query.addBindValue(destination);
            if (!query.exec())
                error = query.lastError().text();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    require(error.isEmpty(), error);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Import a curated manifest into an explicit private library copy; media "
                       "remain read-only."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("data-dir"),
                      QStringLiteral("Private destination profile (required)."),
                      QStringLiteral("directory")});
    parser.addOption({QStringLiteral("manifest"),
                      QStringLiteral("Version 1 curated manifest (required)."),
                      QStringLiteral("file")});
    parser.addOption({QStringLiteral("report"), QStringLiteral("Write aggregate JSON report."),
                      QStringLiteral("file")});
    parser.process(app);
    QString stage;
    try {
        const auto destination = parser.value(QStringLiteral("data-dir"));
        const auto manifestPath = parser.value(QStringLiteral("manifest"));
        require(!destination.isEmpty() && QDir::isAbsolutePath(destination) &&
                    !manifestPath.isEmpty(),
                QStringLiteral("--data-dir must be absolute; --manifest is required"));
#ifdef Q_OS_WIN
        const auto originalProfile =
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/AnimeHub");
#else
        // AnimeHub's AppLocalDataLocation without an organization: ~/.local/share/AnimeHub.
        const auto originalProfile =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            QStringLiteral("/AnimeHub");
#endif
        const auto canonicalDestination = QFileInfo(destination).canonicalFilePath();
        const auto canonicalOriginal = QFileInfo(originalProfile).canonicalFilePath();
        require(normalized(destination) != normalized(originalProfile) &&
                    (canonicalDestination.isEmpty() || canonicalOriginal.isEmpty() ||
                     normalized(canonicalDestination) != normalized(canonicalOriginal)),
                QStringLiteral("This trial importer refuses the live AnimeHub profile"));
        const auto manifest = readObject(manifestPath);
        require(manifest.value(QStringLiteral("version")).toInt() == 1 &&
                    manifest.value(QStringLiteral("entries")).isArray() &&
                    manifest.value(QStringLiteral("franchises")).isArray() &&
                    manifest.value(QStringLiteral("unresolved")).isArray() &&
                    manifest.value(QStringLiteral("catalogCacheFiles")).isArray(),
                QStringLiteral("Invalid manifest schema"));
        require(manifest.value(QStringLiteral("unresolved")).toArray().isEmpty(),
                QStringLiteral("Unresolved entries: complete curation before importing"));
        QMap<QString, Catalog> catalog;
        const QDir manifestDirectory(QFileInfo(manifestPath).absolutePath());
        for (const auto& cachePath :
             manifest.value(QStringLiteral("catalogCacheFiles")).toArray()) {
            require(cachePath.isString(), QStringLiteral("Invalid cache path"));
            const auto cache = readObject(manifestDirectory.filePath(cachePath.toString()));
            auto anime = api::json::decodeAnime(
                QJsonDocument(cache.value(QStringLiteral("anime")).toObject()).toJson());
            require(anime.has_value(), anime ? QString{} : anime.error());
            require(anime->slug == cache.value(QStringLiteral("slug")).toString() &&
                        !catalog.contains(anime->slug),
                    QStringLiteral("Duplicate or mismatched cache slug"));
            require(QDateTime::fromString(cache.value(QStringLiteral("fetchedAt")).toString(),
                                          Qt::ISODateWithMs)
                        .isValid(),
                    QStringLiteral("Missing cache fetch time"));
            Catalog item{*anime, {}};
            QSet<double> numbers;
            const auto pages = cache.value(QStringLiteral("episodePages")).toArray();
            require(!pages.isEmpty(), QStringLiteral("Missing episode pages"));
            int expectedPage = 1;
            int totalRecords = -1;
            for (const auto& page : pages) {
                auto decoded = api::json::decodeEpisodes(QJsonDocument(page.toObject()).toJson());
                require(decoded.has_value(), decoded ? QString{} : decoded.error());
                require(decoded->page == expectedPage++ && decoded->totalPages == pages.size(),
                        QStringLiteral("Incomplete episode pagination"));
                if (totalRecords < 0)
                    totalRecords = decoded->totalRecords;
                require(totalRecords == decoded->totalRecords,
                        QStringLiteral("Inconsistent episode totals"));
                for (const auto& episode : decoded->items) {
                    require(!numbers.contains(episode.number),
                            QStringLiteral("Duplicate provider episode"));
                    numbers.insert(episode.number);
                    item.episodes.append(episode);
                }
            }
            require(item.episodes.size() == totalRecords,
                    QStringLiteral("Incomplete catalog episode list"));
            catalog.insert(anime->slug, std::move(item));
        }
        QSet<QString> paths;
        QSet<QString> usedSlugs;
        qint64 bindings = 0;
        qint64 watchedFiles = 0;
        const auto entries = manifest.value(QStringLiteral("entries")).toArray();
        require(!entries.isEmpty(), QStringLiteral("Empty manifest"));
        for (const auto& value : entries) {
            const auto entry = value.toObject();
            const auto path = entry.value(QStringLiteral("path")).toString();
            const auto slug = entry.value(QStringLiteral("animeSlug")).toString();
            const auto size = entry.value(QStringLiteral("size")).toInteger(-1);
            const QFileInfo info(path);
            // QFileInfo truncates Windows' 100 ns timestamps to milliseconds, whereas the ISO
            // parser rounds longer fractions. Compare at the filesystem API's precision.
            auto timestamp = entry.value(QStringLiteral("mtime")).toString();
            timestamp.replace(QRegularExpression(QStringLiteral("(\\.\\d{3})\\d+")),
                              QStringLiteral("\\1"));
            const auto modifiedAt = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
            require(entry.value(QStringLiteral("watched")).isBool() && catalog.contains(slug),
                    QStringLiteral("Invalid watch flag or unknown slug"));
            require(QDir::isAbsolutePath(path) && !paths.contains(normalized(path)),
                    QStringLiteral("Duplicate or relative media path"));
            require(info.isFile() && info.size() == size && modifiedAt.isValid() &&
                        info.lastModified().toMSecsSinceEpoch() == modifiedAt.toMSecsSinceEpoch(),
                    QStringLiteral("Media changed since inventory: %1").arg(path));
            paths.insert(normalized(path));
            usedSlugs.insert(slug);
            const auto episodes = entry.value(QStringLiteral("episodeNumbers")).toArray();
            require(!episodes.isEmpty(), QStringLiteral("Missing explicit episode binding"));
            QSet<double> unique;
            for (const auto& number : episodes) {
                require(number.isDouble() && std::isfinite(number.toDouble()) &&
                            !unique.contains(number.toDouble()),
                        QStringLiteral("Invalid episode number"));
                unique.insert(number.toDouble());
                bool found = false;
                for (const auto& episode : catalog[slug].episodes)
                    found |= episode.number == number.toDouble();
                require(found, QStringLiteral("Episode absent from provider cache: %1/%2")
                                   .arg(slug)
                                   .arg(number.toDouble()));
                ++bindings;
            }
            watchedFiles += entry.value(QStringLiteral("watched")).toBool();
        }
        QSet<QString> groupKeys, groupTitles, groupedSlugs;
        QMap<QString, QString> memberGroups;
        for (const auto& value : manifest.value(QStringLiteral("franchises")).toArray()) {
            const auto group = value.toObject();
            const auto key = group.value(QStringLiteral("key")).toString();
            const auto title = group.value(QStringLiteral("title")).toString().trimmed();
            require(!key.isEmpty() && !title.isEmpty() && !groupKeys.contains(key) &&
                        !groupTitles.contains(title.toCaseFolded()) &&
                        group.value(QStringLiteral("explicit")).toBool(),
                    QStringLiteral("Invalid or ambiguous franchise"));
            groupKeys.insert(key);
            groupTitles.insert(title.toCaseFolded());
            require(group.value(QStringLiteral("members")).isArray() &&
                        !group.value(QStringLiteral("members")).toArray().isEmpty(),
                    QStringLiteral("Empty franchise"));
            for (const auto& member : group.value(QStringLiteral("members")).toArray()) {
                require(member.isString() && usedSlugs.contains(member.toString()) &&
                            !groupedSlugs.contains(member.toString()),
                        QStringLiteral("Unknown or multiply grouped member"));
                groupedSlugs.insert(member.toString());
                memberGroups.insert(member.toString(), key);
            }
        }
        for (const auto& value : entries) {
            const auto entry = value.toObject();
            require(entry.value(QStringLiteral("franchiseKey")).toString() ==
                        memberGroups.value(entry.value(QStringLiteral("animeSlug")).toString()),
                    QStringLiteral("Entry franchise key disagrees with explicit membership"));
        }
        require(QDir().mkpath(destination), QStringLiteral("Cannot create private profile"));
        QLockFile lock(QDir(destination).filePath(QStringLiteral("import.lock")));
        require(lock.tryLock(0), QStringLiteral("Another import owns this profile"));
        const auto target = QDir(destination).filePath(QStringLiteral("library.sqlite3"));
        require(!QFile::exists(target + QStringLiteral("-wal")) &&
                    !QFile::exists(target + QStringLiteral("-shm")),
                QStringLiteral("Destination must be closed (WAL/SHM exists)"));
        stage = QDir(destination)
                    .filePath(QStringLiteral("import-%1.sqlite3")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (QFile::exists(target))
            snapshot(target, stage);
        {
            auto database = checked(storage::Database::open(stage));
            library::LibraryRepository repository(*database);
            for (const auto& slug : usedSlugs) {
                const auto existing = checked(repository.anime(slug));
                checked(repository.saveSnapshot(library::snapshotOf(catalog[slug].anime)));
                if (!existing || !existing->inLibrary)
                    checked(repository.setInLibrary(slug, true));
                checked(repository.syncEpisodes(slug, catalog[slug].episodes, false));
            }
            for (const auto& value : entries) {
                const auto entry = value.toObject();
                for (const auto& number : entry.value(QStringLiteral("episodeNumbers")).toArray())
                    checked(repository.importEpisodeFile(
                        entry.value(QStringLiteral("animeSlug")).toString(), number.toDouble(),
                        entry.value(QStringLiteral("path")).toString(),
                        entry.value(QStringLiteral("size")).toInteger(),
                        entry.value(QStringLiteral("watched")).toBool()));
            }
            auto existingGroups = checked(repository.franchises());
            for (const auto& value : manifest.value(QStringLiteral("franchises")).toArray()) {
                const auto group = value.toObject();
                qint64 id = 0;
                for (const auto& existing : existingGroups) {
                    if (existing.title.compare(
                            group.value(QStringLiteral("title")).toString().trimmed(),
                            Qt::CaseInsensitive) == 0) {
                        require(id == 0, QStringLiteral("Ambiguous existing franchise title"));
                        id = existing.id;
                    }
                }
                if (!id)
                    id = checked(repository.createFranchise(
                        group.value(QStringLiteral("title")).toString()));
                for (const auto& member : group.value(QStringLiteral("members")).toArray()) {
                    for (const auto& existing : existingGroups)
                        require(existing.id == id || !existing.members.contains(member.toString()),
                                QStringLiteral("Existing personal franchise conflict"));
                    checked(repository.setFranchiseMember(id, member.toString(), true));
                }
            }
            auto integrity = checked(database->run(QStringLiteral("PRAGMA integrity_check")));
            require(integrity.next() && integrity.value(0).toString() == QStringLiteral("ok"),
                    QStringLiteral("Integrity check failed"));
            auto foreignKeys = checked(database->run(QStringLiteral("PRAGMA foreign_key_check")));
            require(!foreignKeys.next(), QStringLiteral("Foreign key check failed"));
            integrity.finish();
            foreignKeys.finish();
            checked(database->run(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")));
        }
        require(!QFile::exists(stage + QStringLiteral("-wal")),
                QStringLiteral("Staging database still has a WAL"));
        {
            QFile source(stage);
            QSaveFile publication(target);
            publication.setDirectWriteFallback(false);
            require(source.open(QIODevice::ReadOnly) && publication.open(QIODevice::WriteOnly),
                    QStringLiteral("Cannot prepare atomic database publication"));
            while (!source.atEnd()) {
                const auto chunk = source.read(1024 * 1024);
                require(!chunk.isEmpty() && publication.write(chunk) == chunk.size(),
                        QStringLiteral("Cannot copy staged database for atomic publication"));
            }
            require(publication.commit(),
                    QStringLiteral("Atomic database publication failed; destination unchanged"));
        }
        QFile::remove(stage);
        stage.clear();
        const QJsonObject report{{QStringLiteral("status"), QStringLiteral("complete")},
                                 {QStringLiteral("files"), entries.size()},
                                 {QStringLiteral("bindings"), bindings},
                                 {QStringLiteral("anime"), usedSlugs.size()},
                                 {QStringLiteral("franchises"), groupKeys.size()},
                                 {QStringLiteral("watchedFiles"), watchedFiles},
                                 {QStringLiteral("unwatchedFiles"), entries.size() - watchedFiles},
                                 {QStringLiteral("integrity"), QStringLiteral("ok")},
                                 {QStringLiteral("mediaModified"), false}};
        const auto reportBytes = QJsonDocument(report).toJson();
        if (parser.isSet(QStringLiteral("report"))) {
            QSaveFile file(parser.value(QStringLiteral("report")));
            require(file.open(QIODevice::WriteOnly) &&
                        file.write(reportBytes) == reportBytes.size() && file.commit(),
                    QStringLiteral("Import committed but report could not be written"));
        }
        QTextStream(stdout) << reportBytes;
        return 0;
    } catch (const std::exception& error) {
        if (!stage.isEmpty()) {
            QFile::remove(stage);
            QFile::remove(stage + QStringLiteral("-wal"));
            QFile::remove(stage + QStringLiteral("-shm"));
        }
        QTextStream(stderr) << "Import rejected: " << error.what() << '\n';
        return 1;
    }
}
