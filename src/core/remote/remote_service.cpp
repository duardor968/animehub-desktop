#include "remote_service.h"
#include "platform/credentials.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>

namespace animehub::remote {
namespace {
QJsonObject profileJson(const Profile& p) {
    return {{QStringLiteral("id"), p.id},
            {QStringLiteral("label"), p.label},
            {QStringLiteral("engine"), p.engine},
            {QStringLiteral("destination"), p.destination},
            {QStringLiteral("connection"), p.connection},
            {QStringLiteral("device"), p.device},
            {QStringLiteral("isDefault"), p.isDefault},
            {QStringLiteral("audio"), p.audio}};
}
Profile readProfile(const QJsonObject& p) {
    return {p.value(QStringLiteral("id")).toString(),
            p.value(QStringLiteral("label")).toString(),
            p.value(QStringLiteral("engine")).toString(),
            p.value(QStringLiteral("destination")).toString(),
            p.value(QStringLiteral("connection")).toString(),
            p.value(QStringLiteral("device")).toString(),
            p.value(QStringLiteral("isDefault")).toBool(),
            p.value(QStringLiteral("audio")).toString()};
}
QString encoded(const QString& id) {
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}
} // namespace
QString RemoteService::credentialTarget(const QUrl& apiBase, const QString& directory) {
    const auto scope = apiBase.toString().toUtf8() + '\n' + QDir(directory).absolutePath().toUtf8();
    return QStringLiteral("AnimeHub/Remote/") +
           QString::fromLatin1(QCryptographicHash::hash(scope, QCryptographicHash::Sha256).toHex());
}
RemoteService::RemoteService(QNetworkAccessManager& network, QUrl apiBase, QString directory,
                             Enqueue enqueue, QObject* parent)
    : QObject(parent), network_(network), base_(apiBase),
      journalPath_(QDir(directory).filePath(QStringLiteral("remote-journal.json"))),
      enqueue_(std::move(enqueue)) {
    auto path = base_.path();
    if (!path.endsWith(u'/'))
        path += u'/';
    base_.setPath(path + QStringLiteral("remote/"));
    const auto target = credentialTarget(apiBase, directory);
    readCredentials_ = [target]() -> std::optional<Credentials> {
        const auto value = platform::readCredential(target);
        if (!value)
            return std::nullopt;
        return Credentials{value->user, value->secret};
    };
    saveCredentials_ = [target](const Credentials& value) {
        return platform::storeCredential(target, {value.id, value.token});
    };
    timer_.setInterval(15000);
    connect(&timer_, &QTimer::timeout, this, &RemoteService::poll);
}
void RemoteService::setCredentialStore(ReadCredentials read, SaveCredentials save) {
    readCredentials_ = std::move(read);
    saveCredentials_ = std::move(save);
}
void RemoteService::setProfiles(int revision, QList<Profile> profiles) {
    revision_ = revision;
    profiles_ = std::move(profiles);
}
void RemoteService::start() {
    journalValid_ = loadJournal();
    if (const auto saved = readCredentials_())
        credentials_ = *saved;
    if (registered() && journalValid_) {
        timer_.start();
        poll();
    }
    emit changed();
}
void RemoteService::fail(const QString& message) {
    error_ = message;
    emit changed();
}
bool RemoteService::loadJournal() {
    QFile file(journalPath_);
    if (!file.exists()) {
        journal_ = {};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("No se puede leer el registro de solicitudes."));
        return false;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(QStringLiteral("El registro de solicitudes necesita atención."));
        return false;
    }
    journal_ = doc.object();
    deviceName_ = journal_.value(QStringLiteral("deviceName")).toString();
    for (const auto& value : journal_.value(QStringLiteral("requests")).toObject()) {
        const auto entry = value.toObject();
        const auto audio = entry.value(QStringLiteral("profile"))
                               .toObject()
                               .value(QStringLiteral("audio"))
                               .toString();
        if (entry.value(QStringLiteral("state")).toString() ==
                QStringLiteral("requires_attention") &&
            audio != QStringLiteral("sub") && audio != QStringLiteral("dub")) {
            localAttention_ = true;
            fail(QStringLiteral("Una solicitud guardada no conserva su idioma original y no se ha "
                                "añadido a la cola."));
        }
    }
    return true;
}
bool RemoteService::saveJournal() {
    QDir().mkpath(QFileInfo(journalPath_).absolutePath());
    QSaveFile file(journalPath_);
    const auto bytes = QJsonDocument(journal_).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        journalValid_ = false;
        fail(QStringLiteral("No se puede guardar el registro de solicitudes."));
        return false;
    }
    return true;
}
void RemoteService::request(const QByteArray& method, const QString& path, const QJsonObject& body,
                            Response done) {
    QNetworkRequest request(base_.resolved(QUrl(path)));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    if (method != "DELETE")
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!credentials_.token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + credentials_.token);
    ++operations_;
    emit changed();
    // This endpoint has no body. Qt's custom DELETE advertises the supplied JSON length
    // without transmitting it, leaving the server waiting for an incomplete request.
    auto* reply = method == "DELETE"
                      ? network_.deleteResource(request)
                      : network_.sendCustomRequest(
                            request, method,
                            method == "GET" ? QByteArray{}
                                            : QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto payload = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        --operations_;
        if (status < 200 || status >= 300)
            fail(QStringLiteral("No se pudo contactar con AnimeHub. Vuelve a intentarlo."));
        if (status >= 200 && status < 300 && journalValid_ && !localAttention_)
            error_.clear();
        done(status, payload);
        emit changed();
    });
}
void RemoteService::registerDevice(const QString& name) {
    if (registered() || busy() || name.trimmed().isEmpty())
        return;
    if (!journalValid_)
        journalValid_ = loadJournal();
    if (!journalValid_)
        return;
    request("POST", QStringLiteral("devices"), {{QStringLiteral("name"), name.trimmed()}},
            [this, name](int status, const QJsonObject& body) {
                if (status < 200 || status >= 300)
                    return;
                Credentials value{body.value(QStringLiteral("id")).toString(),
                                  body.value(QStringLiteral("token")).toString().toUtf8()};
                if (value.id.isEmpty() || value.token.isEmpty() || !saveCredentials_(value)) {
                    fail(QStringLiteral("No se pudo guardar la credencial del dispositivo."));
                    return;
                }
                credentials_ = value;
                deviceName_ = name.trimmed();
                journal_.insert(QStringLiteral("deviceName"), deviceName_);
                if (!saveJournal())
                    return;
                error_.clear();
                timer_.start();
                poll();
            });
}
void RemoteService::publish(std::function<void(bool)> done) {
    if (revision_ <= 0 || profiles_.isEmpty()) {
        done(false);
        return;
    }
    QJsonArray list;
    for (const auto& p : profiles_) {
        if (p.id.isEmpty() || !QDir::isAbsolutePath(p.destination) ||
            (p.audio != QStringLiteral("sub") && p.audio != QStringLiteral("dub")) ||
            (p.engine != QStringLiteral("direct") && p.engine != QStringLiteral("jdownloader"))) {
            done(false);
            return;
        }
        list.append(QJsonObject{{QStringLiteral("id"), p.id},
                                {QStringLiteral("label"), p.label},
                                {QStringLiteral("engine"), p.engine},
                                {QStringLiteral("isDefault"), p.isDefault}});
    }
    const int revision = revision_;
    request("PUT", QStringLiteral("devices/me/profiles"),
            {{QStringLiteral("revision"), revision}, {QStringLiteral("profiles"), list}},
            [this, revision, done = std::move(done)](int status, const QJsonObject& body) {
                done(status >= 200 && status < 300 &&
                     body.value(QStringLiteral("revision")).toInt() == revision &&
                     revision_ == revision);
            });
}
void RemoteService::generatePairingCode() {
    if (!registered() || busy())
        return;
    publish([this](bool ok) {
        if (!ok) {
            fail(QStringLiteral("Revisa el perfil de descarga antes de vincular."));
            return;
        }
        request("POST", QStringLiteral("devices/me/pairing"), {},
                [this](int status, const QJsonObject& body) {
                    if (status < 200 || status >= 300)
                        return;
                    code_ = body.value(QStringLiteral("code")).toString();
                    expiresAt_ = body.value(QStringLiteral("expiresAt")).toString();
                    error_.clear();
                });
    });
}
QJsonArray RemoteService::links() const {
    return links_.value(QStringLiteral("links")).toArray();
}
void RemoteService::refreshLinks() {
    if (!registered())
        return;
    request("GET", QStringLiteral("devices/me/links"), {},
            [this](int status, const QJsonObject& body) {
                if (status >= 200 && status < 300)
                    links_ = body;
            });
}
void RemoteService::revokeLink(const QString& id) {
    if (!registered() || busy() || id.isEmpty())
        return;
    request("DELETE", QStringLiteral("devices/me/links/") + encoded(id), {},
            [this](int status, const QJsonObject&) {
                if (status >= 200 && status < 300) {
                    refreshLinks();
                    code_.clear();
                    expiresAt_.clear();
                }
            });
}
void RemoteService::poll() {
    if (!registered() || !journalValid_ || polling_)
        return;
    polling_ = true;
    publish([this](bool ok) {
        if (!ok) {
            polling_ = false;
            return;
        }
        const auto entries = journal_.value(QStringLiteral("requests")).toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it.value().toObject().value(QStringLiteral("state")).toString() ==
                QStringLiteral("held"))
                reconcile(it.key());
        request("GET", QStringLiteral("devices/me/inbox"), {},
                [this](int status, const QJsonObject& body) {
                    polling_ = false;
                    if (status >= 200 && status < 300) {
                        for (const auto& value : body.value(QStringLiteral("requests")).toArray())
                            process(value.toObject());
                    }
                });
    });
}
void RemoteService::process(const QJsonObject& body) {
    if (!journalValid_)
        return;
    const auto id = body.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || body.value(QStringLiteral("deviceId")).toString() != credentials_.id)
        return;
    auto entries = journal_.value(QStringLiteral("requests")).toObject();
    if (entries.contains(id))
        return;
    const auto slug = body.value(QStringLiteral("animeSlug")).toString();
    const auto episodes = body.value(QStringLiteral("episodeNumbers")).toArray();
    const auto profileId = body.value(QStringLiteral("profileId")).toString();
    const Profile* match = nullptr;
    for (const auto& p : profiles_)
        if (p.id == profileId)
            match = &p;
    bool valid = match && body.value(QStringLiteral("profileRevision")).toInt() == revision_ &&
                 match->engine == body.value(QStringLiteral("engine")).toString() &&
                 QDir::isAbsolutePath(match->destination) &&
                 QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,1023}$"))
                     .match(slug)
                     .hasMatch() &&
                 !episodes.isEmpty() && episodes.size() <= 10000;
    QSet<double> numbers;
    for (const auto& episode : episodes) {
        const double number = episode.toDouble(-1);
        if (!episode.isDouble() || !std::isfinite(number) || number < 0 || numbers.contains(number))
            valid = false;
        numbers.insert(number);
    }
    if (body.value(QStringLiteral("status")).toString() != QStringLiteral("pending"))
        return;
    if (!valid) {
        request("POST", QStringLiteral("devices/me/inbox/") + encoded(id) + QStringLiteral("/ack"),
                {{QStringLiteral("status"), QStringLiteral("requires_attention")},
                 {QStringLiteral("reason"), QStringLiteral("local_profile_unavailable")}},
                [](int, const QJsonObject&) {});
        return;
    }
    // No side effect is permitted until this intent is durable and the server confirms acceptance.
    entries.insert(id, QJsonObject{{QStringLiteral("state"), QStringLiteral("held")},
                                   {QStringLiteral("request"),
                                    QJsonObject{{QStringLiteral("animeSlug"), slug},
                                                {QStringLiteral("episodeNumbers"), episodes},
                                                {QStringLiteral("profileRevision"), revision_}}},
                                   {QStringLiteral("profile"), profileJson(*match)}});
    journal_.insert(QStringLiteral("requests"), entries);
    if (!saveJournal())
        return;
    reconcile(id);
}
void RemoteService::reconcile(const QString& id) {
    request("GET", QStringLiteral("devices/me/inbox/") + encoded(id), {},
            [this, id](int status, const QJsonObject& body) {
                if (status < 200 || status >= 300 || !journalValid_)
                    return;
                const auto state = body.value(QStringLiteral("status")).toString();
                if (state == QStringLiteral("accepted")) {
                    accepted(id);
                    return;
                }
                if (state == QStringLiteral("pending")) {
                    const auto entry =
                        journal_.value(QStringLiteral("requests")).toObject().value(id).toObject();
                    const auto snapshot = entry.value(QStringLiteral("profile")).toObject();
                    bool current = entry.value(QStringLiteral("request"))
                                       .toObject()
                                       .value(QStringLiteral("profileRevision"))
                                       .toInt() == revision_;
                    bool matched = false;
                    for (const auto& profile : profiles_)
                        if (profileJson(profile) == snapshot)
                            matched = true;
                    current = current && matched;
                    request(
                        "POST",
                        QStringLiteral("devices/me/inbox/") + encoded(id) + QStringLiteral("/ack"),
                        {{QStringLiteral("status"), current ? QStringLiteral("accepted")
                                                            : QStringLiteral("requires_attention")},
                         {QStringLiteral("reason"),
                          current ? QJsonValue(QJsonValue::Undefined)
                                  : QJsonValue(QStringLiteral("local_profile_unavailable"))}},
                        [this, id](int ackStatus, const QJsonObject& ack) {
                            if (ackStatus >= 200 && ackStatus < 300 &&
                                ack.value(QStringLiteral("status")).toString() ==
                                    QStringLiteral("accepted"))
                                accepted(id);
                        });
                    return;
                }
                if (state == QStringLiteral("cancelled") ||
                    state == QStringLiteral("requires_attention")) {
                    auto entries = journal_.value(QStringLiteral("requests")).toObject();
                    auto entry = entries.value(id).toObject();
                    entry.insert(QStringLiteral("state"), state);
                    entries.insert(id, entry);
                    journal_.insert(QStringLiteral("requests"), entries);
                    saveJournal();
                }
            });
}
void RemoteService::accepted(const QString& id) {
    if (!journalValid_)
        return;
    auto entries = journal_.value(QStringLiteral("requests")).toObject();
    auto entry = entries.value(id).toObject();
    if (entry.value(QStringLiteral("state")).toString() != QStringLiteral("held"))
        return;
    const auto audio =
        entry.value(QStringLiteral("profile")).toObject().value(QStringLiteral("audio")).toString();
    if (audio != QStringLiteral("sub") && audio != QStringLiteral("dub")) {
        localAttention_ = true;
        entry.insert(QStringLiteral("state"), QStringLiteral("requires_attention"));
        entries.insert(id, entry);
        journal_.insert(QStringLiteral("requests"), entries);
        saveJournal();
        fail(QStringLiteral(
            "Una solicitud guardada no conserva su idioma original y no se ha añadido a la cola."));
        return;
    }
    const auto request = entry.value(QStringLiteral("request")).toObject();
    Submission submission{id,
                          request.value(QStringLiteral("animeSlug")).toString(),
                          {},
                          readProfile(entry.value(QStringLiteral("profile")).toObject()),
                          request.value(QStringLiteral("profileRevision")).toInt()};
    for (const auto& number : request.value(QStringLiteral("episodeNumbers")).toArray())
        submission.episodes.append(number.toDouble());
    if (!enqueue_ || !enqueue_(submission)) {
        fail(QStringLiteral("Una solicitud recibida necesita atención en Descargas."));
        return;
    }
    entry.insert(QStringLiteral("state"), QStringLiteral("queued"));
    entries.insert(id, entry);
    journal_.insert(QStringLiteral("requests"), entries);
    saveJournal();
}
} // namespace animehub::remote
