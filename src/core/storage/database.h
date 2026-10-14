#ifndef ANIMEHUB_CORE_DATABASE_H
#define ANIMEHUB_CORE_DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <expected>
#include <functional>
#include <memory>

namespace animehub::storage {

struct StorageError {
    QString message;
};

template <class T> using Stored = std::expected<T, StorageError>;

// Owns one SQLite connection (WAL, foreign keys, STRICT tables) and its schema migrations.
// A connection belongs to the thread that opened it.
class Database {
public:
    // `path` ":memory:" opens a private in-memory database.
    [[nodiscard]] static Stored<std::unique_ptr<Database>> open(const QString& path);

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    [[nodiscard]] QSqlDatabase connection() const;
    [[nodiscard]] int schemaVersion() const;

    // Runs `body` inside a transaction; commits only when it succeeds.
    Stored<void> transaction(const std::function<Stored<void>()>& body);

    // Prepares and runs `sql` with positional bindings.
    Stored<QSqlQuery> run(const QString& sql, const QVariantList& bindings = {}) const;

private:
    explicit Database(QString connection_name);
    Stored<void> migrate();

    QString connection_name_;
};

[[nodiscard]] qint64 nowMs();

} // namespace animehub::storage

#endif // ANIMEHUB_CORE_DATABASE_H
