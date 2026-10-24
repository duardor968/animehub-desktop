#include "notifications_linux.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace animehub::app {
namespace {

const auto kService = QStringLiteral("org.freedesktop.Notifications");
const auto kPath = QStringLiteral("/org/freedesktop/Notifications");
const auto kInterface = QStringLiteral("org.freedesktop.Notifications");

// The specification's image-data hint, (iiibiiay): the icon travels with the notification, as
// an AppImage installs no icon the server could look up.
struct ImageData {
    QImage image;
};

QDBusArgument& operator<<(QDBusArgument& argument, const ImageData& data) {
    const auto& image = data.image;
    argument.beginStructure();
    argument << image.width() << image.height() << static_cast<int>(image.bytesPerLine()) << true
             << 8 << 4
             << QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
    argument.endStructure();
    return argument;
}

// Never received; QtDBus needs both directions.
const QDBusArgument& operator>>(const QDBusArgument& argument, ImageData&) {
    return argument;
}

QDBusMessage method(const QString& name) {
    return QDBusMessage::createMethodCall(kService, kPath, kInterface, name);
}

} // namespace

Notifications::Notifications(QImage icon, QObject* parent)
    : QObject(parent), icon_(icon.convertToFormat(QImage::Format_RGBA8888)) {
    qDBusRegisterMetaType<ImageData>();
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return;
    }
    bus.connect(kService, kPath, kInterface, QStringLiteral("ActionInvoked"), this,
                SLOT(handleAction(uint, QString)));
    bus.connect(kService, kPath, kInterface, QStringLiteral("NotificationClosed"), this,
                SLOT(handleClosed(uint)));
    // A server that reads markup in the body would take an anime's "&" or "<" for it.
    auto* capabilities =
        new QDBusPendingCallWatcher(bus.asyncCall(method(QStringLiteral("GetCapabilities"))), this);
    connect(capabilities, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher* watcher) {
                const QDBusPendingReply<QStringList> reply = *watcher;
                markup_ = reply.isValid() && reply.value().contains(u"body-markup");
                watcher->deleteLater();
            });
}

void Notifications::show(const QString& title, const QString& text, const QString& tag) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return;
    }
    QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral(ANIMEHUB_APP_ID)}};
    if (!icon_.isNull()) {
        hints.insert(QStringLiteral("image-data"), QVariant::fromValue(ImageData{icon_}));
    }
    auto notify = method(QStringLiteral("Notify"));
    // The "default" action is a click on the notification itself.
    notify << QStringLiteral("AnimeHub") << 0U << QString() << title
           << (markup_ ? text.toHtmlEscaped() : text)
           << QStringList{QStringLiteral("default"), QStringLiteral("Abrir")} << hints << -1;
    auto* sent = new QDBusPendingCallWatcher(bus.asyncCall(notify), this);
    connect(sent, &QDBusPendingCallWatcher::finished, this,
            [this, tag](QDBusPendingCallWatcher* watcher) {
                const QDBusPendingReply<uint> reply = *watcher;
                if (reply.isValid()) {
                    shown_.insert(reply.value(), tag);
                }
                watcher->deleteLater();
            });
}

void Notifications::handleAction(uint id, const QString& action) {
    if (action == u"default" && shown_.contains(id)) {
        emit activated(shown_.value(id));
    }
}

void Notifications::handleClosed(uint id) {
    shown_.remove(id);
}

} // namespace animehub::app
