#ifndef ANIMEHUB_CORE_PLATFORM_INSTALLED_APPS_H
#define ANIMEHUB_CORE_PLATFORM_INSTALLED_APPS_H

#include <QString>

// Where the optional programs AnimeHub works with are installed. Empty when they are not.
namespace animehub::platform {

// JDownloader 2's launcher: on Windows from its installer's entry, on Linux ~/jd2/JDownloader2.
[[nodiscard]] QString jdownloaderExecutable();

// PotPlayer's executable, as its installer records it (Windows only).
[[nodiscard]] QString potPlayerExecutable();

// The program the system opens files with this extension ("mp4") with.
[[nodiscard]] QString defaultApplication(const QString& extension);

// Whether `executable` is PotPlayer (PotPlayerMini64.exe, PotPlayerMini.exe, PotPlayer64.exe…).
[[nodiscard]] bool isPotPlayer(const QString& executable);

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_INSTALLED_APPS_H
