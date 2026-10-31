#include "player_window.h"

namespace animehub::platform {

// PotPlayer is a Windows program: there is no window to follow.
std::optional<PlayerWindowSample> samplePotPlayer(qint64) {
    return std::nullopt;
}

bool closePotPlayer(qint64, std::chrono::milliseconds) {
    return true;
}

std::unique_ptr<QObject> watchExit(qint64, std::function<void()>) {
    return nullptr;
}

} // namespace animehub::platform
