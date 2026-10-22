#include "database.h"

#include <QDateTime>
#include <QDir>
#include <QSqlError>
#include <QUuid>

#include <array>

namespace animehub::storage {
namespace {

// Each step moves the schema from version `index` to `index + 1`. Steps are append-only.
constexpr std::array kMigrations = {
    R"sql(
CREATE TABLE anime (
    slug              TEXT PRIMARY KEY,
    title             TEXT NOT NULL,
    alternative_title TEXT NOT NULL DEFAULT '',
    synopsis          TEXT NOT NULL DEFAULT '',
    poster_url        TEXT NOT NULL DEFAULT '',
    backdrop_url      TEXT NOT NULL DEFAULT '',
    category          TEXT NOT NULL DEFAULT '',
    status            TEXT NOT NULL DEFAULT 'UNKNOWN',
    episode_count     INTEGER,
    genres            TEXT NOT NULL DEFAULT '',
    start_year        INTEGER,
    in_library        INTEGER NOT NULL DEFAULT 0,
    added_at          INTEGER,
    favorite          INTEGER NOT NULL DEFAULT 0,
    pending           INTEGER NOT NULL DEFAULT 0,
    notify            INTEGER NOT NULL DEFAULT 1,
    auto_download     INTEGER NOT NULL DEFAULT 0,
    last_activity_at  INTEGER,
    refreshed_at      INTEGER,
    episodes_synced_at INTEGER
) STRICT;

CREATE TABLE episode (
    slug         TEXT NOT NULL REFERENCES anime(slug) ON DELETE CASCADE,
    number       REAL NOT NULL,
    title        TEXT NOT NULL DEFAULT '',
    image_url    TEXT NOT NULL DEFAULT '',
    published_at INTEGER,
    known_at     INTEGER NOT NULL,
    is_new       INTEGER NOT NULL DEFAULT 0,
    state        INTEGER NOT NULL DEFAULT 0,
    position_ms  INTEGER NOT NULL DEFAULT 0,
    duration_ms  INTEGER NOT NULL DEFAULT 0,
    state_at     INTEGER,
    file_path    TEXT,
    file_size    INTEGER,
    PRIMARY KEY (slug, number)
) STRICT;

CREATE TABLE download (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    slug         TEXT NOT NULL,
    number       REAL NOT NULL,
    anime_title  TEXT NOT NULL,
    audio        TEXT NOT NULL DEFAULT 'SUB',
    origin       TEXT NOT NULL DEFAULT 'manual',
    state        TEXT NOT NULL,
    provider     TEXT,
    source_id    TEXT,
    target_path  TEXT NOT NULL,
    bytes_done   INTEGER NOT NULL DEFAULT 0,
    bytes_total  INTEGER NOT NULL DEFAULT 0,
    error_code   TEXT,
    error_detail TEXT,
    attempts     INTEGER NOT NULL DEFAULT 0,
    retry_at     INTEGER,
    queue_order  INTEGER NOT NULL,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    finished_at  INTEGER
) STRICT;

CREATE UNIQUE INDEX download_active_episode ON download(slug, number, audio)
    WHERE state NOT IN ('completed', 'cancelled');
CREATE UNIQUE INDEX download_active_target ON download(target_path COLLATE NOCASE)
    WHERE state NOT IN ('completed', 'cancelled');
CREATE INDEX episode_activity ON episode(slug, state, state_at);
)sql",
    // An anime not started yet is already "pending": the flag goes.
    R"sql(
ALTER TABLE anime DROP COLUMN pending;
)sql",
    // Resuming all brings back only what pausing all stopped, not what was paused on its own.
    R"sql(
ALTER TABLE download ADD COLUMN paused_with_all INTEGER NOT NULL DEFAULT 0;
)sql",
    R"sql(
ALTER TABLE anime ADD COLUMN archived INTEGER NOT NULL DEFAULT 0;
ALTER TABLE episode ADD COLUMN source_episode_id TEXT NOT NULL DEFAULT '';
CREATE TABLE franchise (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL
) STRICT;
CREATE TABLE franchise_member (
    franchise_id INTEGER NOT NULL REFERENCES franchise(id) ON DELETE CASCADE,
    slug TEXT NOT NULL REFERENCES anime(slug) ON DELETE CASCADE,
    included INTEGER NOT NULL DEFAULT 1,
    manual INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(franchise_id, slug)
) STRICT;
CREATE UNIQUE INDEX franchise_one_membership ON franchise_member(slug) WHERE included = 1;
CREATE TABLE media_file (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL,
    path_key TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL DEFAULT 0
) STRICT;
CREATE TABLE episode_file (
    slug TEXT NOT NULL,
    number REAL NOT NULL,
    file_id INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    PRIMARY KEY(slug, number, file_id),
    FOREIGN KEY(slug, number) REFERENCES episode(slug, number) ON DELETE CASCADE
) STRICT;
ALTER TABLE download ADD COLUMN engine TEXT NOT NULL DEFAULT 'native';
ALTER TABLE download ADD COLUMN engine_endpoint TEXT NOT NULL DEFAULT '';
ALTER TABLE download ADD COLUMN engine_device TEXT NOT NULL DEFAULT '';
ALTER TABLE download ADD COLUMN engine_job_id INTEGER NOT NULL DEFAULT 0;
ALTER TABLE download ADD COLUMN engine_link_ids TEXT NOT NULL DEFAULT '[]';
ALTER TABLE download ADD COLUMN engine_submission TEXT NOT NULL DEFAULT '';
ALTER TABLE download ADD COLUMN engine_phase TEXT NOT NULL DEFAULT '';
ALTER TABLE download ADD COLUMN engine_destination TEXT NOT NULL DEFAULT '';
ALTER TABLE download ADD COLUMN remote_request_id TEXT NOT NULL DEFAULT '';
CREATE UNIQUE INDEX download_remote_episode ON download(remote_request_id, number, audio)
    WHERE remote_request_id <> '';
CREATE TABLE download_request_receipt (
    remote_request_id TEXT NOT NULL,
    number REAL NOT NULL,
    audio TEXT NOT NULL,
    slug TEXT NOT NULL,
    task_id INTEGER NOT NULL,
    PRIMARY KEY(remote_request_id, number, audio)
) STRICT;
)sql",
    R"sql(
CREATE TABLE anime_relation (
    source_slug TEXT NOT NULL REFERENCES anime(slug) ON DELETE CASCADE,
    target_slug TEXT NOT NULL REFERENCES anime(slug) ON DELETE CASCADE,
    PRIMARY KEY(source_slug, target_slug)
) STRICT;
CREATE TABLE franchise_candidate (
    slug TEXT PRIMARY KEY REFERENCES anime(slug) ON DELETE CASCADE,
    reason TEXT NOT NULL
) STRICT;
CREATE TABLE personal_exclusion (
    slug TEXT PRIMARY KEY REFERENCES anime(slug) ON DELETE CASCADE
) STRICT;
)sql",
    R"sql(
CREATE INDEX episode_file_file_id ON episode_file(file_id);
)sql",
};

QString errorText(const QSqlError& error) {
    return error.text().trimmed();
}

} // namespace

qint64 nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

Database::Database(QString connection_name) : connection_name_(std::move(connection_name)) {}

Database::~Database() {
    {
        auto db = QSqlDatabase::database(connection_name_, false);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name_);
}

Stored<std::unique_ptr<Database>> Database::open(const QString& path) {
    const auto name = QStringLiteral("animehub-") + QUuid::createUuid().toString(QUuid::Id128);
    std::unique_ptr<Database> database(new Database(name));
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) {
        return std::unexpected(StorageError{errorText(db.lastError())});
    }
    for (const auto* pragma :
         {"PRAGMA foreign_keys = ON", "PRAGMA journal_mode = WAL", "PRAGMA synchronous = NORMAL"}) {
        if (auto result = database->run(QString::fromLatin1(pragma)); !result) {
            return std::unexpected(result.error());
        }
    }
    if (auto migrated = database->migrate(); !migrated) {
        return std::unexpected(migrated.error());
    }
    return database;
}

QSqlDatabase Database::connection() const {
    return QSqlDatabase::database(connection_name_, false);
}

int Database::schemaVersion() const {
    auto result = run(QStringLiteral("PRAGMA user_version"));
    return result && result->next() ? result->value(0).toInt() : -1;
}

Stored<QSqlQuery> Database::run(const QString& sql, const QVariantList& bindings) const {
    QSqlQuery query(connection());
    if (!query.prepare(sql)) {
        return std::unexpected(StorageError{errorText(query.lastError())});
    }
    for (const auto& value : bindings) {
        query.addBindValue(value);
    }
    if (!query.exec()) {
        return std::unexpected(StorageError{errorText(query.lastError())});
    }
    return query;
}

Stored<void> Database::transaction(const std::function<Stored<void>()>& body) {
    auto db = connection();
    if (!db.transaction()) {
        return std::unexpected(StorageError{errorText(db.lastError())});
    }
    auto result = body();
    if (!result) {
        db.rollback();
        return result;
    }
    if (!db.commit()) {
        const auto error = errorText(db.lastError());
        db.rollback();
        return std::unexpected(StorageError{error});
    }
    return {};
}

Stored<void> Database::migrate() {
    const int version = schemaVersion();
    if (version < 0) {
        return std::unexpected(StorageError{QStringLiteral("cannot read the schema version")});
    }
    if (version > static_cast<int>(kMigrations.size())) {
        return std::unexpected(
            StorageError{QStringLiteral("the library was written by a newer AnimeHub (schema %1)")
                             .arg(version)});
    }
    for (auto step = static_cast<std::size_t>(version); step < kMigrations.size(); ++step) {
        auto applied = transaction([&]() -> Stored<void> {
            const auto statements =
                QString::fromUtf8(kMigrations[step]).split(u';', Qt::SkipEmptyParts);
            for (const auto& statement : statements) {
                if (statement.trimmed().isEmpty()) {
                    continue;
                }
                if (auto result = run(statement); !result) {
                    return std::unexpected(result.error());
                }
            }
            if (step == 3) {
                // Use the same Unicode case folding and lexical normalization as new imports.
                auto files = run(
                    QStringLiteral("SELECT slug, number, file_path, COALESCE(file_size, 0) FROM "
                                   "episode WHERE file_path IS NOT NULL AND file_path <> ''"));
                if (!files)
                    return std::unexpected(files.error());
                while (files->next()) {
                    const auto path =
                        QDir::cleanPath(QDir::fromNativeSeparators(files->value(2).toString()));
                    const auto key = path.toCaseFolded();
                    auto file = run(QStringLiteral("INSERT OR IGNORE INTO media_file(path, "
                                                   "path_key, size) VALUES (?, ?, ?)"),
                                    {path, key, files->value(3)});
                    if (!file)
                        return std::unexpected(file.error());
                    auto mapping =
                        run(QStringLiteral("INSERT INTO episode_file(slug, number, file_id) SELECT "
                                           "?, ?, id FROM media_file WHERE path_key = ?"),
                            {files->value(0), files->value(1), key});
                    if (!mapping)
                        return std::unexpected(mapping.error());
                }
                files->finish();
            }
            if (auto result = run(QStringLiteral("PRAGMA user_version = %1").arg(step + 1));
                !result) {
                return std::unexpected(result.error());
            }
            return {};
        });
        if (!applied) {
            return applied;
        }
    }
    return {};
}

} // namespace animehub::storage
