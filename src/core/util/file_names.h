#ifndef ANIMEHUB_CORE_UTIL_FILE_NAMES_H
#define ANIMEHUB_CORE_UTIL_FILE_NAMES_H

#include <QString>

#include <optional>

namespace animehub::util {

// A single path component safe on Windows, macOS and Linux: no separators, reserved characters,
// control characters, reserved device names (CON, NUL, COM1…) or trailing dots and spaces.
// Never empty: falls back to `fallback`.
[[nodiscard]] QString safeFileName(const QString& name,
                                   const QString& fallback = QStringLiteral("AnimeHub"),
                                   int max_length = 120);

// `.mp4`, `.mkv`… taken from a provider's file name; `.mp4` when it has none or it looks odd.
[[nodiscard]] QString videoExtension(const QString& provider_name);

// A work's folder name, at most 80 UTF-16 units: its clean title (`attempt` 0); when another
// work has that name, with its year (1); then with a short suffix stable for the slug (2).
// Pure: no filesystem access.
[[nodiscard]] QString workFolderName(const QString& title, std::optional<int> year,
                                     const QString& slug, int attempt);

// The folder name earlier versions gave a work: its slug, or a hashed form of an odd one.
[[nodiscard]] QString legacyWorkFolderName(const QString& slug);

// An episode in its work's folder: "<title> - 05", at most 120 UTF-16 units. Pure: extensions
// and transfer suffixes are appended afterwards.
[[nodiscard]] QString downloadBasePath(const QString& folder, const QString& anime_title,
                                       double number);

// `path` itself when free, else `name (2).ext`, `name (3).ext`…
[[nodiscard]] QString uniquePath(const QString& path);

// True when `path` is `directory` or inside it, after resolving `.`/`..`.
[[nodiscard]] bool isInside(const QString& path, const QString& directory);

} // namespace animehub::util

#endif // ANIMEHUB_CORE_UTIL_FILE_NAMES_H
