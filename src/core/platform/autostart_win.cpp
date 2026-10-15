#include "autostart.h"

#include <QSettings>

namespace animehub::platform {
namespace {

const auto kEntry = QStringLiteral("AnimeHub");

} // namespace

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
