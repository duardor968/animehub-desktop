#ifndef ANIMEHUB_APP_SHELL_H
#define ANIMEHUB_APP_SHELL_H

#include <QString>

// Hand-offs to the desktop shell.
namespace animehub::app::shell {

// Opens a file or folder with its default application.
bool open(const QString& path);
// Shows a file selected in the file manager.
void reveal(const QString& path);

} // namespace animehub::app::shell

#endif // ANIMEHUB_APP_SHELL_H
