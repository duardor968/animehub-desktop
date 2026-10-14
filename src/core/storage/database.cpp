#include "database.h"

#include <QDateTime>
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
