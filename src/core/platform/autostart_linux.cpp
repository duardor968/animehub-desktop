#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace animehub::platform {
namespace {

// Exec holds a desktop entry string: a backslash is escaped there, and a percent sign would
// start a field code.
QString escape(const QString& command) {
    QString value = command;
    return value.replace(u'\\', QStringLiteral("\\\\")).replace(u'%', QStringLiteral("%%"));
}

QString unescape(QStringView value) {
    QString command;
    for (qsizetype i = 0; i < value.size(); ++i) {
        if ((value[i] == u'\\' || value[i] == u'%') && i + 1 < value.size() &&
            value[i + 1] == value[i]) {
            ++i;
        }
        command += value[i];
    }
    return command;
}

} // namespace

QString autostartLocation() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("autostart/" ANIMEHUB_APP_ID ".desktop"));
}

QString launcherPath() {
    const auto executable = QCoreApplication::applicationFilePath();
    // An AppImage runs from a mount that goes away with it. The variables are inherited by what
    // other AppImages start, so they only count for an executable inside that mount.
    const auto image = qEnvironmentVariable("APPIMAGE");
    const auto mount = qEnvironmentVariable("APPDIR");
    if (!image.isEmpty() && !mount.isEmpty() &&
        executable.startsWith(QDir(mount).absolutePath() + u'/')) {
        return image;
    }
    return executable;
}

QString autostartCommand(const QString& location) {
    QFile entry(location);
    if (!entry.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QString command;
    while (!entry.atEnd()) {
        const auto line = QString::fromUtf8(entry.readLine()).trimmed();
        // Turned off from the desktop's own settings, it starts nothing.
        if (line == u"Hidden=true" || line == u"X-GNOME-Autostart-enabled=false") {
            return {};
        }
        if (command.isEmpty() && line.startsWith(u"Exec=")) {
            command = unescape(QStringView(line).mid(5));
        }
    }
    return command;
}

bool setAutostartCommand(const QString& command, const QString& location) {
    if (command.isEmpty()) {
        return !QFile::exists(location) || QFile::remove(location);
    }
    QDir().mkpath(QFileInfo(location).absolutePath());
    QSaveFile entry(location);
    if (!entry.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    entry.write(QStringLiteral("[Desktop Entry]\n"
                               "Type=Application\n"
                               "Name=AnimeHub\n"
                               "Exec=%1\n"
                               "Icon=" ANIMEHUB_APP_ID "\n"
                               "Terminal=false\n")
                    .arg(escape(command))
                    .toUtf8());
    return entry.commit();
}

} // namespace animehub::platform
