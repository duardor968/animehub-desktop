#ifndef ANIMEHUB_CORE_UTIL_FILE_NAMES_H
#define ANIMEHUB_CORE_UTIL_FILE_NAMES_H

#include <QString>

namespace animehub::util {

// A single path component safe on Windows, macOS and Linux: no separators, reserved characters,
// control characters, reserved device names (CON, NUL, COM1…) or trailing dots and spaces.
// Never empty: falls back to `fallback`.
[[nodiscard]] QString safeFileName(const QString& name,
                                   const QString& fallback = QStringLiteral("AnimeHub"),
                                   int max_length = 120);

// `.mp4`, `.mkv`… taken from a provider's file name; `.mp4` when it has none or it looks odd.
[[nodiscard]] QString videoExtension(const QString& provider_name);

// `path` itself when free, else `name (2).ext`, `name (3).ext`…
[[nodiscard]] QString uniquePath(const QString& path);

// True when `path` is `directory` or inside it, after resolving `.`/`..`.
[[nodiscard]] bool isInside(const QString& path, const QString& directory);

} // namespace animehub::util

#endif // ANIMEHUB_CORE_UTIL_FILE_NAMES_H
