#ifndef ANIMEHUB_APP_JDOWNLOADER_VIEW_H
#define ANIMEHUB_APP_JDOWNLOADER_VIEW_H

#include "app_settings.h"
#include "downloads/myjdownloader.h"

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration>

namespace animehub::app {

// The MyJDownloader account and device the downloads reach JDownloader through, and the
// JDownloader application Ajustes can start.
class JDownloaderView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(QString account READ account NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

public:
    JDownloaderView(AppSettings& settings, downloads::myjd::Client& remote,
                    QString credential_target, QObject* parent = nullptr);

    [[nodiscard]] QString account() const {
        return account_;
    }
    [[nodiscard]] bool connected() const {
        return remote_.isConnected();
    }
    [[nodiscard]] bool busy() const {
        return busy_;
    }
    [[nodiscard]] QVariantList devices() const {
        return devices_;
    }
    [[nodiscard]] QString problem() const {
        return problem_;
    }

    // Reconnects a saved account; quiet when there is none.
    void start();

    Q_INVOKABLE void connectAccount(const QString& email, const QString& password);
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void launch();

signals:
    void changed();

private:
    void fail(downloads::myjd::Failure failure);
    [[nodiscard]] static QString describe(downloads::myjd::Failure failure);

    AppSettings& settings_;
    downloads::myjd::Client& remote_;
    QString credential_target_;
    QString account_;
    QVariantList devices_;
    QString problem_;
    bool busy_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_JDOWNLOADER_VIEW_H
