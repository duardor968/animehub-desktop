#ifndef ANIMEHUB_CORE_LIBRARY_WORK_FOLDERS_H
#define ANIMEHUB_CORE_LIBRARY_WORK_FOLDERS_H

#include "storage/database.h"

#include <QList>
#include <QString>

namespace animehub::library {

using storage::Stored;

// Where a work's new episodes go under the download directory: the folder there that already
// holds its files or unfinished downloads (the one with most of them), so a retitle never splits
// a series. Otherwise a new folder named by its clean title; with its year, then a short suffix,
// when another work or a folder AnimeHub does not know already has that name.
[[nodiscard]] Stored<QString> workFolder(const storage::Database& database,
                                         const QString& directory, const QString& slug,
                                         const QString& title);

struct FolderMove {
    QString slug;
    QString from;
    QString to;
    int files = 0;     // library media paths updated
    int downloads = 0; // download rows updated
};

struct FolderSkip {
    QString slug;
    QString folder;
    QString reason;
    bool retry = true; // tried again at the next call
};

struct FolderMigration {
    QList<FolderMove> moved;
    QList<FolderSkip> skipped;
    bool done = false; // nothing left to retry: later calls do nothing
};

// Once per library: renames the folders directly under `directory` that earlier versions named
// by a work's slug to the name `workFolder` gives it now, and every path the library and the
// download queue store inside them. One folder at a time: renamed, then its paths updated in a
// transaction, renamed back when that fails. A folder handed over to JDownloader, locked, or
// whose new name exists is left as it is and retried at the next call. Files inside are never
// renamed; folders of no known work are not touched. Call before the download queue starts.
Stored<FolderMigration> migrateWorkFolders(storage::Database& database, const QString& directory);

} // namespace animehub::library

#endif // ANIMEHUB_CORE_LIBRARY_WORK_FOLDERS_H
