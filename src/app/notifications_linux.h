#ifndef ANIMEHUB_APP_NOTIFICATIONS_LINUX_H
#define ANIMEHUB_APP_NOTIFICATIONS_LINUX_H

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>

namespace animehub::app {

// Desktop notifications over org.freedesktop.Notifications. Linux desktops show them with or
// without a tray icon (GNOME has none), and clicking one reports the tag it was shown with.
class Notifications final : public QObject {
    Q_OBJECT

public:
    explicit Notifications(QImage icon, QObject* parent = nullptr);

    void show(const QString& title, const QString& text, const QString& tag);

signals:
    void activated(const QString& tag);

private slots:
    void handleAction(uint id, const QString& action);
    void handleClosed(uint id);

private:
    QImage icon_;
    QHash<uint, QString> shown_; // tags of the notifications the server still keeps
    bool markup_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_NOTIFICATIONS_LINUX_H
