#include "trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace animehub::platform {

// Qt follows the freedesktop.org trash: the file goes to <trash>/files and where it came from
// to <trash>/info/<name>.trashinfo, which the desktop's file manager lists.
std::optional<QString> moveToTrash(const QString& path) {
    QString trash_path;
    if (!QFile::moveToTrash(path, &trash_path)) {
        return std::nullopt;
    }
    return trash_path;
}

bool restoreFromTrash(const QString& trash_path, const QString& path) {
    if (!QFile::rename(trash_path, path)) {
        return false;
    }
    // Its .trashinfo would otherwise stay listed in the trash with nothing behind it.
    const QFileInfo data(trash_path);
    if (QDir trash = data.dir(); trash.dirName() == u"files" && trash.cdUp()) {
        QFile::remove(trash.filePath(QStringLiteral("info/") + data.fileName() +
                                     QStringLiteral(".trashinfo")));
    }
    return true;
}

} // namespace animehub::platform
