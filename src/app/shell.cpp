#include "shell.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace animehub::app::shell {

bool open(const QString& path) {
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void reveal(const QString& path) {
#ifdef Q_OS_WIN
    // Explorer parses its own command line: the path goes quoted right after "/select,".
    QProcess explorer;
    explorer.setProgram(QStringLiteral("explorer.exe"));
    explorer.setNativeArguments(
        QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    if (explorer.startDetached()) {
        return;
    }
#endif
    open(QFileInfo(path).absolutePath());
}

} // namespace animehub::app::shell
