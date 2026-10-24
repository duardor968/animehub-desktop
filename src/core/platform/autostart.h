#ifndef ANIMEHUB_CORE_PLATFORM_AUTOSTART_H
#define ANIMEHUB_CORE_PLATFORM_AUTOSTART_H

#include <QString>

namespace animehub::platform {

// What starts when the current user signs in: the Run key of Windows (the installer writes the
// same entry), or the desktop's XDG autostart entry on Linux.
[[nodiscard]] QString autostartLocation();
// The file that starts this AnimeHub: its executable, or the AppImage that holds it.
[[nodiscard]] QString launcherPath();

// The command AnimeHub's entry starts; empty when there is none.
[[nodiscard]] QString autostartCommand(const QString& location = autostartLocation());
// Writes the entry, or removes it with an empty command.
bool setAutostartCommand(const QString& command, const QString& location = autostartLocation());

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_AUTOSTART_H
