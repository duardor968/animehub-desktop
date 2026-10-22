#include "myjdownloader.h"

#include "platform/aes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <cstring>

namespace animehub::downloads::myjd {
namespace {

const auto kAppKey = QStringLiteral("animehub-desktop");
constexpr qint64 kMaxBody = 1024 * 1024;

Failure failureFor(const QString& type) {
    if (type == u"AUTH_FAILED") {
        return Failure::WrongCredentials;
    }
    if (type == u"ERROR_EMAIL_NOT_CONFIRMED") {
        return Failure::EmailNotConfirmed;
    }
    if (type == u"OFFLINE") {
        return Failure::DeviceOffline;
    }
    if (type == u"MAINTENANCE" || type == u"OVERLOAD" || type == u"TOO_MANY_REQUESTS") {
        return Failure::Busy;
    }
    return Failure::Protocol;
}

QByteArray pkcs7(QByteArray data) {
    const auto pad = 16 - data.size() % 16;
    data.append(QByteArray(pad, static_cast<char>(pad)));
    return data;
}

std::optional<QByteArray> unpkcs7(QByteArray data) {
    if (data.isEmpty()) {
        return std::nullopt;
    }
    const auto pad = static_cast<unsigned char>(data.back());
    if (pad == 0 || pad > 16 || pad > data.size()) {
        return std::nullopt;
    }
    for (qsizetype i = data.size() - pad; i < data.size(); ++i) {
        if (static_cast<unsigned char>(data[i]) != pad) {
            return std::nullopt;
        }
    }
    data.chop(pad);
    return data;
}

platform::AesBlock block(const QByteArray& bytes) {
    platform::AesBlock out{};
    std::memcpy(out.data(), bytes.constData(),
                std::min<std::size_t>(16, static_cast<std::size_t>(bytes.size())));
    return out;
}

} // namespace

Secrets Secrets::derive(const QString& email, const QString& password) {
    // MyJDownloader derives its keys from the lower-cased address.
    const auto address = email.trimmed().toLower();
    return {.email = address,
            .login = sha256((address + password + QStringLiteral("server")).toUtf8()),
            .device = sha256((address + password + QStringLiteral("device")).toUtf8())};
}

QByteArray Secrets::serialize() const {
    return login + device;
}

std::optional<Secrets> Secrets::deserialize(const QString& email, const QByteArray& data) {
    if (data.size() != 64) {
        return std::nullopt;
    }
    return Secrets{.email = email, .login = data.left(32), .device = data.mid(32)};
}

QByteArray sha256(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray updateToken(const QByteArray& secret, const QString& session_token) {
    return sha256(secret + QByteArray::fromHex(session_token.toLatin1()));
}

QString sign(const QByteArray& key, const QByteArray& path_and_query) {
    return QString::fromLatin1(
        QMessageAuthenticationCode::hash(path_and_query, key, QCryptographicHash::Sha256).toHex());
}

QByteArray encrypt(const QByteArray& plain, const QByteArray& iv_key) {
    auto data = pkcs7(plain);
    auto iv = block(iv_key.left(16));
    const platform::Aes128 aes(block(iv_key.mid(16, 16)));
    if (!aes.cbcEncrypt(
            {reinterpret_cast<std::uint8_t*>(data.data()), static_cast<std::size_t>(data.size())},
            iv)) {
        return {};
    }
    return data.toBase64();
}

std::optional<QByteArray> decrypt(const QByteArray& base64, const QByteArray& iv_key) {
    auto data = QByteArray::fromBase64(base64.trimmed());
    if (data.isEmpty() || data.size() % 16 != 0) {
        return std::nullopt;
    }
    auto iv = block(iv_key.left(16));
    const platform::Aes128 aes(block(iv_key.mid(16, 16)));
    if (!aes.cbcDecrypt(
            {reinterpret_cast<std::uint8_t*>(data.data()), static_cast<std::size_t>(data.size())},
            iv)) {
        return std::nullopt;
    }
    return unpkcs7(std::move(data));
}

Client::Client(QNetworkAccessManager& network, QObject* parent, QUrl endpoint)
    : QObject(parent), network_(network), endpoint_(std::move(endpoint)) {}

qint64 Client::nextRequestId() {
    // Request ids must grow; the clock alone can repeat within a millisecond.
    last_rid_ = std::max(last_rid_ + 1, QDateTime::currentMSecsSinceEpoch());
    return last_rid_;
}

void Client::callServer(const QString& path, const QByteArray& key,
                        QList<std::pair<QString, QString>> params, QObject* context, Answer done) {
    const auto rid = nextRequestId();
    params.append({QStringLiteral("rid"), QString::number(rid)});
    QByteArray query = path.toUtf8() + '?';
    for (qsizetype i = 0; i < params.size(); ++i) {
        if (i > 0) {
            query += '&';
        }
        query += QUrl::toPercentEncoding(params[i].first) + '=' +
                 QUrl::toPercentEncoding(params[i].second);
    }
    const auto signature = sign(key, query);
    const auto url = QUrl::fromEncoded(
        endpoint_.toEncoded() + query + "&signature=" + signature.toLatin1(), QUrl::StrictMode);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json; charset=utf-8"));
    request.setTransferTimeout(20'000);
    auto* reply = network_.post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context, [reply, key, rid, done = std::move(done)] {
        const auto body = reply->read(kMaxBody);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            const auto error = QJsonDocument::fromJson(body).object();
            const auto type = error.value(u"type").toString();
            done(std::unexpected(
                std::pair{status == 0 ? Failure::Network : failureFor(type), type}));
            return;
        }
        const auto plain = decrypt(body, key);
        const auto object = plain ? QJsonDocument::fromJson(*plain).object() : QJsonObject();
        if (object.value(u"rid").toInteger() != rid) {
            done(std::unexpected(std::pair{Failure::Protocol, QStringLiteral("rid")}));
            return;
        }
        done(object);
    });
}

void Client::adoptSession(const QJsonObject& answer) {
    const auto previous_server_token = server_token_;
    session_token_ = answer.value(u"sessiontoken").toString();
    regain_token_ = answer.value(u"regaintoken").toString();
    // A fresh login chains from the login secret; a reconnection from the last server token.
    server_token_ = updateToken(
        previous_server_token.isEmpty() ? secrets_.login : previous_server_token, session_token_);
    device_token_ = updateToken(secrets_.device, session_token_);
}

void Client::connectAccount(const Secrets& secrets, QObject* context, Callback<void> done) {
    secrets_ = secrets;
    session_token_.clear();
    server_token_.clear();
    callServer(QStringLiteral("/my/connect"), secrets_.login,
               {{QStringLiteral("email"), secrets_.email}, {QStringLiteral("appkey"), kAppKey}},
               context, [this, done = std::move(done)](const auto& answer) {
                   if (!answer) {
                       done(std::unexpected(answer.error().first));
                       return;
                   }
                   adoptSession(*answer);
                   done({});
               });
}

void Client::reconnect(QObject* context, std::function<void(bool)> done) {
    callServer(QStringLiteral("/my/reconnect"), server_token_,
               {{QStringLiteral("appkey"), kAppKey},
                {QStringLiteral("sessiontoken"), session_token_},
                {QStringLiteral("regaintoken"), regain_token_}},
               context, [this, done = std::move(done)](const auto& answer) {
                   if (!answer) {
                       done(false);
                       return;
                   }
                   adoptSession(*answer);
                   done(true);
               });
}

void Client::devices(QObject* context, Callback<QList<Device>> done) {
    if (!isConnected()) {
        done(std::unexpected(Failure::WrongCredentials));
        return;
    }
    requestDevices(context, std::move(done), false);
}

void Client::requestDevices(QObject* context, Callback<QList<Device>> done, bool retried) {
    callServer(QStringLiteral("/my/listdevices"), server_token_,
               {{QStringLiteral("sessiontoken"), session_token_}}, context,
               [this, context, done, retried](const auto& answer) {
                   if (!answer && answer.error().second == u"TOKEN_INVALID" && !retried) {
                       reconnect(context, [this, context, done](bool ok) {
                           if (ok) {
                               requestDevices(context, done, true);
                           } else {
                               done(std::unexpected(Failure::WrongCredentials));
                           }
                       });
                       return;
                   }
                   if (!answer) {
                       done(std::unexpected(answer.error().first));
                       return;
                   }
                   QList<Device> list;
                   for (const auto& item : answer->value(u"list").toArray()) {
                       const auto object = item.toObject();
                       list.append({.id = object.value(u"id").toString(),
                                    .name = object.value(u"name").toString()});
                   }
                   done(list);
               });
}

void Client::callDevice(const QString& device_id, const QString& path, const QJsonArray& params,
                        QObject* context, Answer done, bool retried) {
    QJsonArray arguments;
    for (const auto& parameter : params) {
        const auto json = QJsonDocument(QJsonArray{parameter}).toJson(QJsonDocument::Compact);
        arguments.append(QString::fromUtf8(json.mid(1, json.size() - 2)));
    }
    const auto rid = nextRequestId();
    const QJsonObject payload{{QStringLiteral("apiVer"), 1},
                              {QStringLiteral("rid"), rid},
                              {QStringLiteral("url"), path},
                              {QStringLiteral("params"), arguments}};
    const auto key = device_token_;
    const auto body = encrypt(QJsonDocument(payload).toJson(QJsonDocument::Compact), key);
    const auto url =
        QUrl::fromEncoded(endpoint_.toEncoded() + "/t_" + QUrl::toPercentEncoding(session_token_) +
                              '_' + QUrl::toPercentEncoding(device_id) + path.toUtf8(),
                          QUrl::StrictMode);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json; charset=utf-8"));
    request.setTransferTimeout(20'000);
    auto* reply = network_.post(request, body);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(
        reply, &QNetworkReply::finished, context,
        [this, reply, key, rid, device_id, path, params, context, retried, done = std::move(done)] {
            const auto answer = reply->read(kMaxBody);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) {
                const auto type =
                    QJsonDocument::fromJson(answer).object().value(u"type").toString();
                if (type == u"TOKEN_INVALID" && !retried) {
                    reconnect(context, [this, device_id, path, params, context, done](bool ok) {
                        if (ok) {
                            callDevice(device_id, path, params, context, done, true);
                        } else {
                            done(std::unexpected(std::pair{Failure::WrongCredentials,
                                                           QStringLiteral("TOKEN_INVALID")}));
                        }
                    });
                    return;
                }
                done(std::unexpected(
                    std::pair{status == 0 ? Failure::Network : failureFor(type), type}));
                return;
            }
            const auto plain = decrypt(answer, key);
            const auto object = plain ? QJsonDocument::fromJson(*plain).object() : QJsonObject();
            if (object.value(u"rid").toInteger() != rid) {
                done(std::unexpected(std::pair{Failure::Protocol, QStringLiteral("rid")}));
                return;
            }
            done(object);
        });
}

void Client::addLinks(const QString& device_id, const QString& package, const QList<QUrl>& links,
                      QObject* context, Callback<void> done) {
    if (!isConnected()) {
        done(std::unexpected(Failure::WrongCredentials));
        return;
    }
    QStringList urls;
    for (const auto& link : links) {
        urls.append(link.toString(QUrl::FullyEncoded));
    }
    callDevice(device_id, QStringLiteral("/linkgrabberv2/addLinks"),
               {QJsonObject{{QStringLiteral("links"), urls.join(QLatin1Char('\n'))},
                            {QStringLiteral("packageName"), package},
                            {QStringLiteral("autostart"), false}}},
               context, [done = std::move(done)](const auto& answer) {
                   if (!answer) {
                       done(std::unexpected(answer.error().first));
                       return;
                   }
                   done({});
               });
}

void Client::request(const QString& device_id, const QString& path, const QJsonArray& params,
                     QObject* context, Callback<QJsonValue> done) {
    if (!isConnected() || device_id.isEmpty()) {
        done(std::unexpected(Failure::WrongCredentials));
        return;
    }
    callDevice(device_id, path, params, context, [done = std::move(done)](const auto& answer) {
        if (!answer) {
            done(std::unexpected(answer.error().first));
        } else if (!answer->contains(u"data")) {
            done(std::unexpected(Failure::Protocol));
        } else {
            done(answer->value(u"data"));
        }
    });
}

void Client::disconnectAccount() {
    if (isConnected()) {
        // Best effort: the session expires by itself anyway.
        callServer(QStringLiteral("/my/disconnect"), server_token_,
                   {{QStringLiteral("sessiontoken"), session_token_}}, this, [](const auto&) {});
    }
    session_token_.clear();
    regain_token_.clear();
    server_token_.clear();
    device_token_.clear();
}

} // namespace animehub::downloads::myjd
