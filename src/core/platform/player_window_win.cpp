#include "player_window.h"

#include <QWinEventNotifier>

#include <array>

#include <windows.h>

namespace animehub::platform {
namespace {

// PotPlayer's remote-control messages (WM_USER + command), answered by its main window.
constexpr WPARAM kGetPlayStatus = 0x5006;  // -1 stopped or switching, 1 paused, 2 playing
constexpr WPARAM kGetTotalTime = 0x5002;   // ms
constexpr WPARAM kGetCurrentTime = 0x5004; // ms
constexpr UINT kAnswerTimeoutMs = 200;
const auto kTitleSuffix = QStringLiteral(" - PotPlayer");

struct Search {
    DWORD pid = 0;
    HWND window = nullptr;
};

BOOL CALLBACK findMain(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<Search*>(parameter);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search.pid) {
        return TRUE;
    }
    std::array<wchar_t, 64> name{};
    const int length = GetClassNameW(window, name.data(), static_cast<int>(name.size()));
    const auto type = QString::fromWCharArray(name.data(), length);
    // 64-bit PotPlayer's main window, and the 32-bit one's.
    if (type == u"PotPlayer64" || type == u"PotPlayer") {
        search.window = window;
        return FALSE;
    }
    return TRUE;
}

std::optional<LRESULT> ask(HWND window, WPARAM command) {
    DWORD_PTR answer = 0;
    if (!SendMessageTimeoutW(window, WM_USER, command, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                             kAnswerTimeoutMs, &answer)) {
        return std::nullopt;
    }
    return static_cast<LRESULT>(answer);
}

QString titleOf(HWND window) {
    std::array<wchar_t, 1024> text{};
    DWORD_PTR length = 0;
    if (!SendMessageTimeoutW(window, WM_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, kAnswerTimeoutMs, &length)) {
        return {};
    }
    return QString::fromWCharArray(text.data(), static_cast<qsizetype>(length));
}

} // namespace

std::optional<PlayerWindowSample> samplePotPlayer(qint64 pid) {
    Search search{.pid = static_cast<DWORD>(pid)};
    EnumWindows(findMain, reinterpret_cast<LPARAM>(&search));
    if (!search.window) {
        return std::nullopt;
    }
    const auto before = titleOf(search.window);
    const auto status = ask(search.window, kGetPlayStatus);
    const auto position = ask(search.window, kGetCurrentTime);
    const auto duration = ask(search.window, kGetTotalTime);
    // The same title on both sides: the figures belong to that file, not to the next one.
    if (!status || !position || !duration || (*status != 1 && *status != 2) || *duration <= 0 ||
        !before.endsWith(kTitleSuffix) || titleOf(search.window) != before) {
        return std::nullopt;
    }
    return PlayerWindowSample{.fileName = before.chopped(kTitleSuffix.size()),
                              .positionMs = static_cast<qint64>(*position),
                              .durationMs = static_cast<qint64>(*duration)};
}

bool closePotPlayer(qint64 pid, std::chrono::milliseconds wait) {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return true; // already gone
    }
    Search search{.pid = static_cast<DWORD>(pid)};
    EnumWindows(findMain, reinterpret_cast<LPARAM>(&search));
    if (search.window) {
        PostMessageW(search.window, WM_CLOSE, 0, 0);
    }
    const bool ended =
        WaitForSingleObject(process, static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0;
    CloseHandle(process);
    return ended;
}

namespace {

// Owns the process handle the notifier waits on.
class ExitWatch final : public QObject {
public:
    ExitWatch(HANDLE process, std::function<void()> exited)
        : process_(process), notifier_(process) {
        connect(&notifier_, &QWinEventNotifier::activated, this,
                [this, exited = std::move(exited)] {
                    notifier_.setEnabled(false);
                    exited();
                });
    }
    ~ExitWatch() override {
        notifier_.setEnabled(false);
        CloseHandle(process_);
    }

private:
    HANDLE process_;
    QWinEventNotifier notifier_;
};

} // namespace

std::unique_ptr<QObject> watchExit(qint64 pid, std::function<void()> exited) {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return nullptr;
    }
    return std::make_unique<ExitWatch>(process, std::move(exited));
}

} // namespace animehub::platform
