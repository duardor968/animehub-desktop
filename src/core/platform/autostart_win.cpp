#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <array>
#include <vector>

#include <windows.h>

namespace animehub::platform {
namespace {

const auto kEntry = QStringLiteral("AnimeHub");
constexpr const wchar_t* kEntryWide = L"AnimeHub";
constexpr BYTE kStartupDisabledBit = 0x01U;

struct RegKey {
    HKEY handle = nullptr;

    RegKey() = default;
    ~RegKey() {
        if (handle != nullptr) {
            RegCloseKey(handle);
        }
    }
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
};

// Maps a HKCU Run key (e.g. HKEY_CURRENT_USER\Software\...\Run) to its sibling
// Explorer\StartupApproved\Run subkey, keeping unit tests isolated under their own test hive path.
QString startupApprovedSubkey(const QString& key) {
    QString normalized = key;
    normalized.replace(QLatin1Char('/'), QLatin1Char('\\'));
    const auto prefix = QStringLiteral(R"(HKEY_CURRENT_USER\)");
    if (!normalized.startsWith(prefix, Qt::CaseInsensitive)) {
        return {};
    }
    const auto subkey = normalized.mid(prefix.size());
    const auto last_sep = subkey.lastIndexOf(QLatin1Char('\\'));
    if (last_sep <= 0 || last_sep + 1 >= subkey.size()) {
        return {};
    }
    const auto parent = subkey.left(last_sep);
    const auto leaf = subkey.mid(last_sep + 1);
    return parent + QStringLiteral(R"(\Explorer\StartupApproved\)") + leaf;
}

// Windows records Task Manager / Settings startup overrides as REG_BINARY in StartupApproved\Run;
// bit 0 of byte 0 is set (e.g. 0x03 or 0x07) when the user disabled the entry.
bool isDisabledInStartupApproved(const QString& approved_subkey) {
    if (approved_subkey.isEmpty()) {
        return false;
    }
    RegKey key;
    const auto wide_subkey = approved_subkey.toStdWString();
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wide_subkey.c_str(), 0, KEY_QUERY_VALUE, &key.handle) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key.handle, kEntryWide, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_BINARY || size < 1) {
        return false;
    }
    std::vector<BYTE> data(size);
    if (RegQueryValueExW(key.handle, kEntryWide, nullptr, &type, data.data(), &size) !=
            ERROR_SUCCESS ||
        type != REG_BINARY || size < 1) {
        return false;
    }
    return (data[0] & kStartupDisabledBit) != 0U;
}

void clearStartupApprovedOverride(const QString& approved_subkey) {
    if (approved_subkey.isEmpty()) {
        return;
    }
    RegKey key;
    const auto wide_subkey = approved_subkey.toStdWString();
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wide_subkey.c_str(), 0, KEY_SET_VALUE, &key.handle) !=
        ERROR_SUCCESS) {
        return;
    }
    RegDeleteValueW(key.handle, kEntryWide);
}

QString extractExecutablePath(const QString& command) {
    const auto trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (trimmed.startsWith(QLatin1Char('"'))) {
        const auto closing = trimmed.indexOf(QLatin1Char('"'), 1);
        return closing > 1 ? trimmed.mid(1, closing - 1) : trimmed.mid(1);
    }
    for (qsizetype i = 0; i < trimmed.size(); ++i) {
        if (trimmed[i].isSpace()) {
            return trimmed.left(i);
        }
    }
    return trimmed;
}

bool isDevOrTempPath(const QString& executable_path) {
    if (executable_path.isEmpty()) {
        return false;
    }
    const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(executable_path)).toLower();
    if (normalized.contains(QStringLiteral("/out/build/")) ||
        normalized.endsWith(QStringLiteral("/out/build")) ||
        normalized.contains(QStringLiteral("/appdata/local/temp/"))) {
        return true;
    }
    const std::array temp_roots = {
        QDir::tempPath(),
        qEnvironmentVariable("TEMP"),
        qEnvironmentVariable("TMP"),
    };
    for (const auto& candidate : temp_roots) {
        if (candidate.isEmpty()) {
            continue;
        }
        auto root = QDir::cleanPath(QDir::fromNativeSeparators(candidate)).toLower();
        while (root.size() > 1 && root.endsWith(QLatin1Char('/'))) {
            root.chop(1);
        }
        if (!root.isEmpty() &&
            (normalized == root || normalized.startsWith(root + QLatin1Char('/')))) {
            return true;
        }
    }
    return false;
}

} // namespace

QString autostartLocation() {
    return QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)");
}

QString launcherPath() {
    return QCoreApplication::applicationFilePath();
}

QString autostartCommand(const QString& key) {
    const auto command = QSettings(key, QSettings::NativeFormat).value(kEntry).toString();
    if (command.isEmpty()) {
        return {};
    }
    if (isDisabledInStartupApproved(startupApprovedSubkey(key))) {
        return {};
    }
    return command;
}

bool setAutostartCommand(const QString& command, const QString& key) {
    const auto approved_subkey = startupApprovedSubkey(key);
    if (command.isEmpty()) {
        QSettings run(key, QSettings::NativeFormat);
        run.remove(kEntry);
        run.sync();
        clearStartupApprovedOverride(approved_subkey);
        return run.status() == QSettings::NoError;
    }

    // Never let a dev build (out\build\...) or temp copy overwrite or register user autostart.
    if (isDevOrTempPath(extractExecutablePath(command))) {
        return false;
    }

    QSettings run(key, QSettings::NativeFormat);
    run.setValue(kEntry, command);
    run.sync();
    if (run.status() != QSettings::NoError) {
        return false;
    }
    // Clear any disabled override from Task Manager so enabling from Settings takes effect.
    clearStartupApprovedOverride(approved_subkey);
    return true;
}

} // namespace animehub::platform
