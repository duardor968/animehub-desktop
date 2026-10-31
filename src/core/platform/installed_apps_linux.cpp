#include "installed_apps.h"

#include <QDir>
#include <QFileInfo>

namespace animehub::platform {

QString jdownloaderExecutable() {
    // JDownloader's Linux installer puts its launcher script in ~/jd2 by default.
    const QFileInfo script(QDir::home().filePath(QStringLiteral("jd2/JDownloader2")));
    return script.isFile() && script.isExecutable() ? script.absoluteFilePath() : QString();
}

// PotPlayer, and following its window, exist only on Windows.
QString potPlayerExecutable() {
    return {};
}

QString defaultApplication(const QString&) {
    return {};
}

bool isPotPlayer(const QString&) {
    return false;
}

} // namespace animehub::platform
