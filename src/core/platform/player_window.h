#ifndef ANIMEHUB_CORE_PLATFORM_PLAYER_WINDOW_H
#define ANIMEHUB_CORE_PLATFORM_PLAYER_WINDOW_H

#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

// Following an external player that has no IPC of its own (PotPlayer) through its main window.
// Windows only; on Linux nothing is found.
namespace animehub::platform {

struct PlayerWindowSample {
    QString fileName; // the file on view, from the window title
    qint64 positionMs = 0;
    qint64 durationMs = 0;
};

// What PotPlayer's window of process `pid` shows while it plays or is paused; none while it
// stops, switches files or has no window. Each question waits at most ~200 ms, so a modal dialog
// of PotPlayer never blocks the caller; it may run on any thread.
[[nodiscard]] std::optional<PlayerWindowSample> samplePotPlayer(qint64 pid);

// Asks PotPlayer's window of process `pid` to close, as its own close button does, and waits up
// to `wait` for the process to end. It never forces it: a PotPlayer asking something stays open.
// True once it ended.
bool closePotPlayer(qint64 pid, std::chrono::milliseconds wait);

// Calls `exited` on the caller's thread once process `pid` ends; destroying the returned object
// stops watching. Null when the process cannot be watched (already gone, or no support).
[[nodiscard]] std::unique_ptr<QObject> watchExit(qint64 pid, std::function<void()> exited);

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_PLAYER_WINDOW_H
