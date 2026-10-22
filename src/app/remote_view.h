#pragma once
#include "remote/remote_service.h"
#include <QVariantList>
#include <QtQmlIntegration>
namespace animehub::app {
class RemoteView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(bool registered READ registered NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    Q_PROPERTY(QString expiresAt READ expiresAt NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList links READ links NOTIFY changed)
public:
    explicit RemoteView(remote::RemoteService& service, QObject* parent = nullptr);
    bool registered() const {
        return service_.registered();
    }
    QString deviceName() const {
        return service_.deviceName();
    }
    QString code() const {
        return service_.code();
    }
    QString expiresAt() const {
        return service_.expiresAt();
    }
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
    Q_INVOKABLE void revokeLink(const QString& id) {
        service_.revokeLink(id);
    }
signals:
    void changed();

private:
    remote::RemoteService& service_;
};
} // namespace animehub::app
