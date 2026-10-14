#include "jdownloader_view.h"

#include "platform/credentials.h"

#include <QVariantMap>

namespace animehub::app {

using downloads::myjd::Failure;

JDownloaderView::JDownloaderView(AppSettings& settings, downloads::ClickNLoad& local,
                                 downloads::myjd::Client& remote, QString credential_target,
                                 QObject* parent)
    : QObject(parent), settings_(settings), local_(local), remote_(remote),
      credential_target_(std::move(credential_target)) {}

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
        // Only the derived keys are kept, by Windows; the password is forgotten here.
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

void JDownloaderView::send(const QString& package, const QList<QUrl>& links, QObject* context,
                           std::function<void(bool, const QString&)> done) {
    if (settings_.jdownloader() == AppSettings::JDownloader::Remote) {
        if (!remote_.isConnected() || settings_.jdownloaderDevice().isEmpty()) {
            done(false, QStringLiteral("Conecta tu cuenta de MyJDownloader en Ajustes."));
            return;
        }
        remote_.addLinks(
            settings_.jdownloaderDevice(), package, links, context, [done](const auto& result) {
                done(result.has_value(), result ? QStringLiteral("Enlaces enviados a JDownloader.")
                                                : describe(result.error()));
            });
        return;
    }
    local_.send(package, links, context, [done](const auto& result) {
        if (result) {
            done(true, QStringLiteral("Enlaces enviados a JDownloader."));
            return;
        }
        switch (result.error()) {
        case downloads::ClickNLoad::Failure::Unreachable:
            done(false, QStringLiteral("Abre JDownloader con Click'n'Load activo (puerto 9666)."));
            break;
        case downloads::ClickNLoad::Failure::Foreign:
            done(false, QStringLiteral("Otro programa está usando el puerto 9666."));
            break;
        case downloads::ClickNLoad::Failure::Rejected:
            done(false, QStringLiteral("JDownloader no aceptó los enlaces."));
            break;
        }
    });
}

} // namespace animehub::app
