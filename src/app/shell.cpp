#include "shell.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <dwmapi.h>
#endif
#ifdef Q_OS_LINUX
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QStringList>
#endif

namespace animehub::app::shell {

bool open(const QString& path) {
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void reveal(const QString& path) {
#ifdef Q_OS_WIN
    // Explorer parses its own command line: the path goes quoted right after "/select,".
    QProcess explorer;
    explorer.setProgram(QStringLiteral("explorer.exe"));
    explorer.setNativeArguments(
        QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    if (explorer.startDetached()) {
        return;
    }
#endif
#ifdef Q_OS_LINUX
    // The file managers' own interface (Nautilus, Dolphin, Nemo, Thunar...) selects the file;
    // without one, the folder opens.
    if (auto bus = QDBusConnection::sessionBus(); bus.isConnected()) {
        const auto service = QStringLiteral("org.freedesktop.FileManager1");
        auto show =
            QDBusMessage::createMethodCall(service, QStringLiteral("/org/freedesktop/FileManager1"),
                                           service, QStringLiteral("ShowItems"));
        show << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
        auto* call = new QDBusPendingCallWatcher(bus.asyncCall(show), QCoreApplication::instance());
        QObject::connect(call, &QDBusPendingCallWatcher::finished, call,
                         [path](QDBusPendingCallWatcher* watcher) {
                             if (watcher->isError()) {
                                 open(QFileInfo(path).absolutePath());
                             }
                             watcher->deleteLater();
                         });
        return;
    }
#endif
    open(QFileInfo(path).absolutePath());
}

void tintTitleBar(QWindow* window, const QColor& background, const QColor& text) {
#ifdef Q_OS_WIN
    if (window == nullptr) {
        return;
    }
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    // Attributes of Windows 11 (build 22000+); older systems ignore them.
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kTextColor = 36;
    const COLORREF caption = RGB(background.red(), background.green(), background.blue());
    const COLORREF title = RGB(text.red(), text.green(), text.blue());
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kTextColor, &title, sizeof(title));
#else
    // Elsewhere the title bar is the desktop's own.
    Q_UNUSED(window);
    Q_UNUSED(background);
    Q_UNUSED(text);
#endif
}

} // namespace animehub::app::shell
