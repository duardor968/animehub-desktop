#include "installed_apps.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <array>

#include <windows.h>

#include <shlwapi.h>

namespace animehub::platform {
namespace {

QString registryValue(const QString& key, const QString& name) {
    return QSettings(key, QSettings::NativeFormat).value(name).toString();
}

QString existing(const QString& path) {
    return !path.isEmpty() && QFileInfo(path).isFile()
               ? QDir::cleanPath(QFileInfo(path).absoluteFilePath())
               : QString();
}

} // namespace

QString jdownloaderExecutable() {
    for (const auto* root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        const auto folder = registryValue(
            QString::fromLatin1(root) +
                QStringLiteral("\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                               "jdownloader2"),
            QStringLiteral("InstallLocation"));
        if (const auto path = existing(QDir(folder).filePath(QStringLiteral("JDownloader2.exe")));
            !folder.isEmpty() && !path.isEmpty()) {
            return path;
        }
    }
    // Where its installer puts it by default.
    return existing(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                        .filePath(QStringLiteral("JDownloader 2/JDownloader2.exe")));
}

QString potPlayerExecutable() {
    for (const auto* key : {"HKEY_CURRENT_USER\\Software\\DAUM\\PotPlayer64",
                            "HKEY_CURRENT_USER\\Software\\DAUM\\PotPlayer"}) {
        if (const auto path =
                existing(registryValue(QString::fromLatin1(key), QStringLiteral("ProgramPath")));
            isPotPlayer(path)) {
            return path;
        }
    }
    return {};
}

QString defaultApplication(const QString& extension) {
    const auto dotted = (QStringLiteral(".") + extension).toStdWString();
    std::array<wchar_t, MAX_PATH * 2> buffer{};
    auto size = static_cast<DWORD>(buffer.size());
    // With the "open" verb: without it, a choice made in "Open with" is not found.
    if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, dotted.c_str(), L"open",
                                 buffer.data(), &size))) {
        return {};
    }
    return existing(QString::fromWCharArray(buffer.data()));
}

bool isPotPlayer(const QString& executable) {
    return QFileInfo(executable)
        .fileName()
        .startsWith(QStringLiteral("PotPlayer"), Qt::CaseInsensitive);
}

} // namespace animehub::platform
