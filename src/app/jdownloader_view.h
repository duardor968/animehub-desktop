#ifndef ANIMEHUB_APP_JDOWNLOADER_VIEW_H
#define ANIMEHUB_APP_JDOWNLOADER_VIEW_H

#include "app_settings.h"
#include "downloads/myjdownloader.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration>

#include <functional>

namespace animehub::app {

// The MyJDownloader account and device the downloads reach JDownloader through, the
// JDownloader application Ajustes can start, and the setup that connects it.
class JDownloaderView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(QString account READ account NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    // The JDownloader of this computer: the one chosen in Ajustes, else the one installed; empty
    // when there is none.
    Q_PROPERTY(QString executable READ executable NOTIFY changed)
    // The setup's connection test: running, and why the last one failed.
    Q_PROPERTY(bool testing READ testing NOTIFY changed)
    Q_PROPERTY(QString testProblem READ testProblem NOTIFY changed)

public:
    // What reaches outside AnimeHub; tests replace it.
    struct System {
        std::function<QString()> findExecutable;
        std::function<bool(const QString& program)> start;
        std::function<void(const QUrl& url)> openUrl;
    };

    JDownloaderView(AppSettings& settings, downloads::myjd::Client& remote,
                    QNetworkAccessManager& network, QString credential_target, System system,
                    QObject* parent = nullptr);

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
    [[nodiscard]] QString executable() const;
    [[nodiscard]] bool testing() const {
        return testing_;
    }
    [[nodiscard]] QString testProblem() const {
        return test_problem_;
    }

    // Reconnects a saved account; quiet when there is none.
    void start();

    Q_INVOKABLE void connectAccount(const QString& email, const QString& password);
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void launch();
    // Looks for an installed JDownloader again.
    Q_INVOKABLE void findExecutable();
    Q_INVOKABLE void openDownloadPage();
    // Asks JDownloader for its version through the local API (`local`) or checks that the
    // MyJDownloader account has a device. On success the downloads use it that way.
    Q_INVOKABLE void testConnection(bool local);
    // Signs in to MyJDownloader with this account, then tests it as testConnection(false).
    Q_INVOKABLE void testAccount(const QString& email, const QString& password);

signals:
    void changed();
    // The setup connected JDownloader.
    void connectionReady();

private:
    void refreshDevices();
    void fail(downloads::myjd::Failure failure);
    void checkDevices();
    void testFailed(const QString& problem);
    void testPassed(AppSettings::JDownloader mode);
    [[nodiscard]] static QString describe(downloads::myjd::Failure failure);

    AppSettings& settings_;
    downloads::myjd::Client& remote_;
    QNetworkAccessManager& network_;
    QString credential_target_;
    System system_;
    QString account_;
    QVariantList devices_;
    QString problem_;
    QString found_; // the installed JDownloader, as last looked for
    QString test_problem_;
    bool busy_ = false;
    bool testing_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_JDOWNLOADER_VIEW_H
