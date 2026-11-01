#include "jdownloader_view.h"

#include "downloads/jdownloader.h"
#include "platform/credentials.h"

#include <QFileInfo>
#include <QVariantMap>

namespace animehub::app {

using downloads::myjd::Failure;

JDownloaderView::JDownloaderView(AppSettings& settings, downloads::myjd::Client& remote,
                                 QNetworkAccessManager& network, QString credential_target,
                                 System system, QObject* parent)
    : QObject(parent), settings_(settings), remote_(remote), network_(network),
      credential_target_(std::move(credential_target)), system_(std::move(system)) {
    connect(&settings_, &AppSettings::changed, this, &JDownloaderView::changed);
}

QString JDownloaderView::executable() const {
    const auto chosen = settings_.jdExecutable();
    return QFileInfo(chosen).isFile() ? chosen : found_;
}

void JDownloaderView::findExecutable() {
    const auto found = system_.findExecutable();
    if (found != found_) {
        found_ = found;
        emit changed();
    }
}

void JDownloaderView::openDownloadPage() {
    system_.openUrl(QUrl(QStringLiteral("https://jdownloader.org/download/index")));
}

QString JDownloaderView::describe(Failure failure) {
    switch (failure) {
    case Failure::WrongCredentials:
        return QStringLiteral("El correo o la contraseña de MyJDownloader no son correctos.");
    case Failure::EmailNotConfirmed:
        return QStringLiteral(
            "Confirma tu cuenta de MyJDownloader desde el correo que te enviaron.");
    case Failure::DeviceOffline:
        return QStringLiteral("JDownloader no está conectado a MyJDownloader en ese equipo.");
    case Failure::Busy:
        return QStringLiteral("MyJDownloader no responde ahora mismo. Prueba en unos minutos.");
    case Failure::Network:
        return QStringLiteral("No hay conexión con MyJDownloader.");
    case Failure::Protocol:
        return QStringLiteral("MyJDownloader respondió algo inesperado.");
    }
    return {};
}

void JDownloaderView::fail(Failure failure) {
    busy_ = false;
    problem_ = describe(failure);
    emit changed();
}

void JDownloaderView::start() {
    const auto saved = platform::readCredential(credential_target_);
    if (!saved) {
        return;
    }
    const auto secrets = downloads::myjd::Secrets::deserialize(saved->user, saved->secret);
    if (!secrets) {
        return;
    }
    account_ = saved->user;
    busy_ = true;
    emit changed();
    remote_.connectAccount(*secrets, this, [this](const auto& result) {
        if (!result) {
            fail(result.error());
            return;
        }
        refreshDevices();
    });
}

void JDownloaderView::launch() {
    const QFileInfo executable(this->executable());
    // Windows starts a program by its .exe; on Linux JDownloader2 is a script marked executable.
#ifdef Q_OS_WIN
    const bool runnable =
        executable.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0;
#else
    const bool runnable = executable.isExecutable();
#endif
    if (!executable.isAbsolute() || !executable.isFile() || !runnable ||
        !system_.start(executable.absoluteFilePath())) {
        problem_ = QStringLiteral("Selecciona el ejecutable de JDownloader en Ajustes.");
    } else {
        problem_.clear();
    }
    emit changed();
}

void JDownloaderView::connectAccount(const QString& email, const QString& password) {
    if (email.trimmed().isEmpty() || password.isEmpty()) {
        return;
    }
    const auto secrets = downloads::myjd::Secrets::derive(email, password);
    busy_ = true;
    problem_.clear();
    emit changed();
    remote_.connectAccount(secrets, this, [this, secrets](const auto& result) {
        if (!result) {
            fail(result.error());
            return;
        }
        // Only the derived keys are kept, by the system's store; the password is forgotten here.
        platform::storeCredential(credential_target_,
                                  {.user = secrets.email, .secret = secrets.serialize()});
        account_ = secrets.email;
        refreshDevices();
    });
}

void JDownloaderView::disconnectAccount() {
    remote_.disconnectAccount();
    platform::removeCredential(credential_target_);
    account_.clear();
    devices_.clear();
    problem_.clear();
    settings_.setJdownloaderDevice({});
    emit changed();
}

void JDownloaderView::testConnection(bool local) {
    if (testing_) {
        return;
    }
    testing_ = true;
    test_problem_.clear();
    emit changed();
    if (local) {
        downloads::Task probe;
        probe.engine = downloads::DownloadEngine::JDownloaderLocal;
        probe.engineEndpoint = settings_.jdEndpoint();
        downloads::jd::request(network_, nullptr, probe, QStringLiteral("/jd/version"), {}, this,
                               [this](const auto& answer) {
                                   if (!answer) {
                                       testFailed(QStringLiteral(
                                           "JDownloader no responde en este PC. Revisa los pasos "
                                           "y vuelve a probar."));
                                       return;
                                   }
                                   testPassed(AppSettings::JDownloader::Local);
                               });
        return;
    }
    if (!remote_.isConnected()) {
        testFailed(QStringLiteral("Conecta tu cuenta de MyJDownloader."));
        return;
    }
    checkDevices();
}

void JDownloaderView::testAccount(const QString& email, const QString& password) {
    if (testing_ || email.trimmed().isEmpty() || password.isEmpty()) {
        return;
    }
    testing_ = true;
    test_problem_.clear();
    emit changed();
    const auto secrets = downloads::myjd::Secrets::derive(email, password);
    remote_.connectAccount(secrets, this, [this, secrets](const auto& result) {
        if (!result) {
            testFailed(describe(result.error()));
            return;
        }
        // As Ajustes keeps it: only the derived keys, by the system's store.
        platform::storeCredential(credential_target_,
                                  {.user = secrets.email, .secret = secrets.serialize()});
        account_ = secrets.email;
        checkDevices();
    });
}

void JDownloaderView::checkDevices() {
    remote_.devices(this, [this](const auto& result) {
        if (!result) {
            testFailed(describe(result.error()));
            return;
        }
        if (result->isEmpty()) {
            testFailed(QStringLiteral("Tu cuenta no tiene ningún JDownloader conectado."));
            return;
        }
        devices_.clear();
        for (const auto& device : *result) {
            devices_.append(QVariantMap{{QStringLiteral("id"), device.id},
                                        {QStringLiteral("name"), device.name}});
        }
        const auto saved = settings_.jdownloaderDevice();
        if (std::ranges::none_of(*result, [&](const auto& d) { return d.id == saved; })) {
            settings_.setJdownloaderDevice(result->first().id);
        }
        testPassed(AppSettings::JDownloader::Remote);
    });
}

void JDownloaderView::testFailed(const QString& problem) {
    testing_ = false;
    test_problem_ = problem;
    emit changed();
}

void JDownloaderView::testPassed(AppSettings::JDownloader mode) {
    testing_ = false;
    test_problem_.clear();
    settings_.setJdownloader(mode);
    emit changed();
    emit connectionReady();
}

void JDownloaderView::refreshDevices() {
    busy_ = true;
    emit changed();
    remote_.devices(this, [this](const auto& result) {
        if (!result) {
            fail(result.error());
            return;
        }
        devices_.clear();
        for (const auto& device : *result) {
            devices_.append(QVariantMap{{QStringLiteral("id"), device.id},
                                        {QStringLiteral("name"), device.name}});
        }
        // One device, or the saved one gone: choose for the user.
        const auto saved = settings_.jdownloaderDevice();
        const bool known =
            std::ranges::any_of(*result, [&](const auto& d) { return d.id == saved; });
        if (!known && !result->isEmpty()) {
            settings_.setJdownloaderDevice(result->first().id);
        }
        busy_ = false;
        problem_ = result->isEmpty()
                       ? QStringLiteral("Tu cuenta no tiene ningún JDownloader conectado.")
                       : QString();
        emit changed();
    });
}

} // namespace animehub::app
