#ifndef ANIMEHUB_CORE_PLATFORM_AUTOSTART_H
#define ANIMEHUB_CORE_PLATFORM_AUTOSTART_H

#include <QString>

namespace animehub::platform {

// What starts when the current user signs in to Windows; the installer writes the same entry.
inline const QString kRunKey =
    QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)");

// The command AnimeHub's entry starts; empty when there is none.
[[nodiscard]] QString autostartCommand(const QString& key = kRunKey);
// Writes the entry, or removes it with an empty command.
bool setAutostartCommand(const QString& command, const QString& key = kRunKey);

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_AUTOSTART_H
