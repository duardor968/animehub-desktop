#ifndef ANIMEHUB_CORE_PLATFORM_TRASH_H
#define ANIMEHUB_CORE_PLATFORM_TRASH_H

#include <QString>

#include <optional>

namespace animehub::platform {

// Moves a file to the Recycle Bin and says where it went there, to bring it back.
[[nodiscard]] std::optional<QString> moveToTrash(const QString& path);
// Brings a file moved by moveToTrash() back to `path`, leaving no entry behind in the bin.
[[nodiscard]] bool restoreFromTrash(const QString& trash_path, const QString& path);

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_TRASH_H
