#include "autostart.h"

#include <QCoreApplication>
#include <QSettings>

namespace animehub::platform {
namespace {

const auto kEntry = QStringLiteral("AnimeHub");

} // namespace

QString autostartLocation() {
    return QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)");
}

QString launcherPath() {
    return QCoreApplication::applicationFilePath();
}

QString autostartCommand(const QString& key) {
    return QSettings(key, QSettings::NativeFormat).value(kEntry).toString();
}

bool setAutostartCommand(const QString& command, const QString& key) {
    QSettings run(key, QSettings::NativeFormat);
    if (command.isEmpty()) {
        run.remove(kEntry);
    } else {
        run.setValue(kEntry, command);
    }
    run.sync();
    return run.status() == QSettings::NoError;
}

} // namespace animehub::platform
