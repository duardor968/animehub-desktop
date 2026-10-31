#include "remote_service.h"
#include "api/api_client.h"
#include "library/library_service.h"
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
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace animehub::remote {
namespace {

using namespace std::chrono_literals;

// Without the live channel, a request waits up to a minute to be seen; each look is a request
// to the API.
constexpr auto kInboxInterval = 1min;
// The API sends a heartbeat every 25 s: a silent minute is a dead connection.
constexpr auto kChannelSilence = 60s;
// Reconnection waits 1 s after a drop, doubling after each failed attempt.
constexpr auto kReconnectLimit = 60s;

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

void rememberAnime(api::ApiClient& api, library::LibraryService& library, const QString& slug,
                   QObject* context, std::function<void(Readiness)> done) {
    if (const auto known = library.repository().anime(slug); known && *known) {
        done(Readiness::Ready);
        return;
    }
    // One answer only: the network's, else the last saved copy.
    api.anime(
        slug, context,
        [&library, done](const api::Result<api::AnimeDetail>& detail) {
            if (!detail) {
                done(detail.error().kind == api::ApiError::Kind::NotFound ? Readiness::Never
                                                                          : Readiness::Later);
                return;
            }
            done(library.remember(library::snapshotOf(detail->value)) ? Readiness::Ready
                                                                      : Readiness::Later);
        },
        api::CachePolicy::NetworkElseCache);
}

RemoteService::RemoteService(QNetworkAccessManager& network, QUrl apiBase, QString directory,
                             Prepare prepare, Enqueue enqueue, QObject* parent)
    : QObject(parent), network_(network), base_(apiBase),
      journalPath_(QDir(directory).filePath(QStringLiteral("remote-journal.json"))),
      prepare_(std::move(prepare)), enqueue_(std::move(enqueue)) {
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
    timer_.setInterval(kInboxInterval);
    connect(&timer_, &QTimer::timeout, this, &RemoteService::poll);
    reconnect_.setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, &RemoteService::openChannel);
}
RemoteService::~RemoteService() {
    if (channel_) {
        channel_->disconnect(this);
        channel_->abort();
        channel_->deleteLater();
    }
}
void RemoteService::setCredentialStore(ReadCredentials read, SaveCredentials save) {
    readCredentials_ = std::move(read);
    saveCredentials_ = std::move(save);
}
void RemoteService::keepCredentialsInProfile() {
    const auto file =
        QFileInfo(journalPath_).dir().filePath(QStringLiteral("remote-credential.json"));
    setCredentialStore(
        [file]() -> std::optional<Credentials> {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly))
                return std::nullopt;
            const auto value = QJsonDocument::fromJson(in.readAll()).object();
            Credentials saved{value.value(QStringLiteral("id")).toString(),
                              value.value(QStringLiteral("token")).toString().toUtf8()};
            if (saved.id.isEmpty() || saved.token.isEmpty())
                return std::nullopt;
            return saved;
        },
        [file](const Credentials& value) {
            QDir().mkpath(QFileInfo(file).absolutePath());
            QSaveFile out(file);
            const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("id"), value.id},
                                                         {QStringLiteral("token"),
                                                          QString::fromUtf8(value.token)}})
                                   .toJson(QJsonDocument::Compact);
            return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() &&
                   out.commit();
        });
}

void RemoteService::setProfiles(int revision, QList<Profile> profiles) {
    const bool changed = revision != revision_;
    revision_ = revision;
    profiles_ = std::move(profiles);
    // A new destination reaches the web at once, not at the next look at the inbox.
    if (changed && started_) {
        poll();
    }
}

void RemoteService::start() {
    journalValid_ = loadJournal();
    if (const auto saved = readCredentials_())
        credentials_ = *saved;
    if (registered() && journalValid_)
        begin();
    emit changed();
}
void RemoteService::begin() {
    started_ = true;
    poll();
    openChannel();
}
void RemoteService::openChannel() {
    if (channel_ || !registered())
        return;
    QNetworkRequest request(base_.resolved(QUrl(QStringLiteral("devices/me/events"))));
    request.setTransferTimeout(std::chrono::milliseconds(kChannelSilence));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Authorization", "Bearer " + credentials_.token);
    channel_ = network_.get(request);
    connect(channel_, &QNetworkReply::readyRead, this, &RemoteService::readChannel);
    connect(channel_, &QNetworkReply::finished, this, &RemoteService::channelClosed);
}
void RemoteService::readChannel() {
    if (!live_) {
        const auto status = channel_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 || !channel_->header(QNetworkRequest::ContentTypeHeader)
                                  .toString()
                                  .startsWith(QStringLiteral("text/event-stream"))) {
            channel_->abort(); // channelClosed() takes it from here
            return;
        }
        // Open: what changed before it opened is read now, and then only what it announces.
        live_ = true;
        channelFailures_ = 0;
        timer_.stop();
        poll();
        refreshLinks(true);
        emit changed();
    }
    channelBuffer_ += channel_->readAll().replace('\r', QByteArray());
    for (qsizetype end; (end = channelBuffer_.indexOf("\n\n")) >= 0;) {
        const auto block = channelBuffer_.left(end);
        channelBuffer_.remove(0, end + 2);
        for (const auto& line : block.split('\n')) {
            if (!line.startsWith("event:"))
                continue;
            const auto type = line.mid(6).trimmed();
            if (type == "inbox")
                poll();
            else if (type == "links")
                refreshLinks(true);
        }
    }
}
void RemoteService::channelClosed() {
    const bool wasLive = live_;
    channel_->deleteLater();
    channel_ = nullptr;
    channelBuffer_.clear();
    live_ = false;
    if (!registered())
        return;
    // Until it is back, the inbox is read every minute; after a drop, also at once.
    if (!timer_.isActive())
        timer_.start();
    if (wasLive)
        poll();
    channelFailures_ = wasLive ? 0 : channelFailures_ + 1;
    reconnect_.start(std::min<std::chrono::milliseconds>(
        kReconnectLimit, std::chrono::seconds(1) * (1 << std::min(channelFailures_, 6))));
    if (wasLive)
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
                            Response done, Mode mode) {
    QNetworkRequest request(base_.resolved(QUrl(path)));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    if (method != "DELETE")
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!credentials_.token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + credentials_.token);
    ++inFlight_;
    if (mode == Mode::User) {
        ++operations_;
        emit changed();
    }
    // This endpoint has no body. Qt's custom DELETE advertises the supplied JSON length
    // without transmitting it, leaving the server waiting for an incomplete request.
    auto* reply = method == "DELETE"
                      ? network_.deleteResource(request)
                      : network_.sendCustomRequest(
                            request, method,
                            method == "GET" ? QByteArray{}
                                            : QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, mode, done = std::move(done)] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto payload = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        --inFlight_;
        if (mode == Mode::Quiet) {
            done(status, payload);
            return;
        }
        if (mode == Mode::User)
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
                begin();
            });
}
void RemoteService::publish(Mode mode, std::function<void(bool)> done) {
    if (revision_ <= 0 || profiles_.isEmpty()) {
        done(false);
        return;
    }
    // The API keeps the profiles it took; the same revision would only repeat them.
    if (publishedRevision_ == revision_) {
        done(true);
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
    request(
        "PUT", QStringLiteral("devices/me/profiles"),
        {{QStringLiteral("revision"), revision}, {QStringLiteral("profiles"), list}},
        [this, revision, done = std::move(done)](int status, const QJsonObject& body) {
            const bool taken = status >= 200 && status < 300 &&
                               body.value(QStringLiteral("revision")).toInt() == revision;
            if (taken) {
                publishedRevision_ = revision;
            }
            done(taken && revision_ == revision);
        },
        mode);
}
void RemoteService::generatePairingCode() {
    if (!registered() || busy())
        return;
    publish(Mode::User, [this](bool ok) {
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
void RemoteService::refreshLinks(bool quiet) {
    if (!registered())
        return;
    request(
        "GET", QStringLiteral("devices/me/links"), {},
        [this, quiet](int status, const QJsonObject& body) {
            if (status < 200 || status >= 300 || body == links_)
                return;
            links_ = body;
            if (quiet)
                emit changed();
        },
        quiet ? Mode::Quiet : Mode::User);
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
    if (!registered() || !journalValid_)
        return;
    // A change announced while the inbox is being read may not be in that answer.
    if (polling_) {
        pollAgain_ = true;
        return;
    }
    polling_ = true;
    const auto done = [this] {
        polling_ = false;
        if (std::exchange(pollAgain_, false))
            poll();
    };
    publish(Mode::Background, [this, done](bool ok) {
        if (!ok) {
            done();
            return;
        }
        const auto entries = journal_.value(QStringLiteral("requests")).toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it.value().toObject().value(QStringLiteral("state")).toString() ==
                QStringLiteral("held"))
                reconcile(it.key());
        request(
            "GET", QStringLiteral("devices/me/inbox"), {},
            [this, done](int status, const QJsonObject& body) {
                if (status >= 200 && status < 300) {
                    for (const auto& value : body.value(QStringLiteral("requests")).toArray())
                        process(value.toObject());
                }
                done();
            },
            Mode::Background);
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
        acknowledge(id, QStringLiteral("local_profile_unavailable"));
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
    request(
        "GET", QStringLiteral("devices/me/inbox/") + encoded(id), {},
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
                if (!current) {
                    acknowledge(id, QStringLiteral("local_profile_unavailable"));
                    return;
                }
                // Accepted only once the PC can record it: an anime it does not know yet
                // comes first, or the request waits, held, for the next look.
                const auto slug = entry.value(QStringLiteral("request"))
                                      .toObject()
                                      .value(QStringLiteral("animeSlug"))
                                      .toString();
                prepare_(slug, [self = QPointer(this), id](Readiness readiness) {
                    if (!self || !self->journalValid_ || readiness == Readiness::Later) {
                        return;
                    }
                    self->acknowledge(id, readiness == Readiness::Ready
                                              ? QString()
                                              : QStringLiteral("local_record_unavailable"));
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
        },
        Mode::Background);
}
void RemoteService::acknowledge(const QString& id, const QString& problem) {
    const bool accept = problem.isEmpty();
    request(
        "POST", QStringLiteral("devices/me/inbox/") + encoded(id) + QStringLiteral("/ack"),
        {{QStringLiteral("status"),
          accept ? QStringLiteral("accepted") : QStringLiteral("requires_attention")},
         {QStringLiteral("reason"), accept ? QJsonValue(QJsonValue::Undefined) : problem}},
        [this, id](int status, const QJsonObject& ack) {
            // A lost answer leaves the request held: the next look asks for it again.
            if (status >= 200 && status < 300 &&
                ack.value(QStringLiteral("status")).toString() == QStringLiteral("accepted")) {
                accepted(id);
            }
        },
        Mode::Background);
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
