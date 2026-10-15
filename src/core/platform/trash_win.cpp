#include "trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace animehub::platform {

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
    // The bin keeps a file as $R… and its original name and date in a matching $I…, which
    // would otherwise stay listed with nothing behind it.
    const QFileInfo data(trash_path);
    if (data.fileName().startsWith(QLatin1String("$R"))) {
        QFile::remove(data.dir().filePath(QStringLiteral("$I") + data.fileName().mid(2)));
    }
    return true;
}

} // namespace animehub::platform
