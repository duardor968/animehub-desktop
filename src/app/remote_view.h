#pragma once
#include "remote/remote_service.h"
#include <QSet>
#include <QSysInfo>
#include <QVariantList>
#include <QtQmlIntegration>
namespace animehub::app {
// Ajustes › Dispositivos vinculados. A pairing code shows until a browser links with it.
class RemoteView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(bool registered READ registered NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    // What linking offers to call this PC until the user names it: the computer's own name.
    Q_PROPERTY(QString suggestedName READ suggestedName CONSTANT)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    // When the code stops working, "12:26".
    Q_PROPERTY(QString expiresText READ expiresText NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // [{id, name, since ("27 oct")}]
    Q_PROPERTY(QVariantList links READ links NOTIFY changed)
public:
    explicit RemoteView(remote::RemoteService& service, QObject* parent = nullptr);
    bool registered() const {
        return service_.registered();
    }
    QString deviceName() const {
        return service_.deviceName();
    }
    static QString suggestedName() {
        return QSysInfo::machineHostName();
    }
    QString code() const {
        return service_.code() == used_code_ ? QString() : service_.code();
    }
    QString expiresText() const;
    QString error() const {
        return service_.error();
    }
    bool busy() const {
        return service_.busy();
    }
    QVariantList links() const;
    Q_INVOKABLE void registerDevice(const QString& name) {
        service_.registerDevice(name);
    }
    Q_INVOKABLE void generatePairingCode() {
        service_.generatePairingCode();
    }
    Q_INVOKABLE void refresh() {
        service_.refreshLinks();
    }
    // The code on view, to paste in the browser.
    Q_INVOKABLE void copyCode() const;
    // What a page in view calls every few seconds: a browser that links shows at once.
    Q_INVOKABLE void watch() {
        service_.refreshLinks(true);
    }
    Q_INVOKABLE void revokeLink(const QString& id) {
        service_.revokeLink(id);
    }
signals:
    void changed();
    // A browser linked with the code on view; the code is spent.
    void linked();

private:
    void update();
    [[nodiscard]] QSet<QString> linkIds() const;

    remote::RemoteService& service_;
    QString code_seen_;        // the code on view, and
    QSet<QString> ids_before_; // the browsers linked when it came
    QString used_code_;
};
} // namespace animehub::app
