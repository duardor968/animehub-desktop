#ifndef ANIMEHUB_APP_SHELL_H
#define ANIMEHUB_APP_SHELL_H

#include <QColor>
#include <QString>

class QWindow;

// Hand-offs to the desktop shell.
namespace animehub::app::shell {

// Opens a file or folder with its default application.
bool open(const QString& path);
// Shows a file selected in the file manager.
void reveal(const QString& path);
// Paints the native title bar (Windows 11) so it continues the window's own top bar.
void tintTitleBar(QWindow* window, const QColor& background, const QColor& text);

} // namespace animehub::app::shell

#endif // ANIMEHUB_APP_SHELL_H
