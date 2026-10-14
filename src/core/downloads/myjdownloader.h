#ifndef ANIMEHUB_CORE_DOWNLOADS_MYJDOWNLOADER_H
#define ANIMEHUB_CORE_DOWNLOADS_MYJDOWNLOADER_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <expected>
#include <functional>

class QNetworkAccessManager;

// Sends links to a JDownloader anywhere through the MyJDownloader service: requests signed with
// HMAC-SHA256 and bodies encrypted with AES-128-CBC, with keys derived from the account.
namespace animehub::downloads::myjd {

// Keys derived from the account; the password itself is never kept.
struct Secrets {
    QString email;
    QByteArray login;  // SHA-256(email + password + "server")
    QByteArray device; // SHA-256(email + password + "device")

    [[nodiscard]] static Secrets derive(const QString& email, const QString& password);
    [[nodiscard]] QByteArray serialize() const;
    [[nodiscard]] static std::optional<Secrets> deserialize(const QString& email,
                                                            const QByteArray& data);
};

struct Device {
    QString id;
    QString name;
};

enum class Failure {
    WrongCredentials,  // AUTH_FAILED
    EmailNotConfirmed, // the account still has to be confirmed
    DeviceOffline,     // JDownloader is not running or not connected
    Busy,              // maintenance, overload or too many requests
    Network,
    Protocol,
};

// Primitives, public so they can be checked against the reference implementation.
[[nodiscard]] QByteArray sha256(const QByteArray& data);
[[nodiscard]] QByteArray updateToken(const QByteArray& secret, const QString& session_token);
[[nodiscard]] QString sign(const QByteArray& key, const QByteArray& path_and_query);
// AES-128-CBC with PKCS#7: the first half of `iv_key` is the IV, the second the key.
[[nodiscard]] QByteArray encrypt(const QByteArray& plain, const QByteArray& iv_key);
[[nodiscard]] std::optional<QByteArray> decrypt(const QByteArray& base64, const QByteArray& iv_key);

class Client final : public QObject {
    Q_OBJECT

public:
    explicit Client(QNetworkAccessManager& network, QObject* parent = nullptr,
                    QUrl endpoint = QUrl(QStringLiteral("https://api.jdownloader.org")));

    template <class T> using Callback = std::function<void(const std::expected<T, Failure>&)>;

    void connectAccount(const Secrets& secrets, QObject* context, Callback<void> done);
    void devices(QObject* context, Callback<QList<Device>> done);
    void addLinks(const QString& device_id, const QString& package, const QList<QUrl>& links,
                  QObject* context, Callback<void> done);
    void disconnectAccount();
    [[nodiscard]] bool isConnected() const {
        return !session_token_.isEmpty();
    }

private:
    using Answer =
        std::function<void(const std::expected<QJsonObject, std::pair<Failure, QString>>&)>;

    void callServer(const QString& path, const QByteArray& key,
                    QList<std::pair<QString, QString>> params, QObject* context, Answer done);
    void callDevice(const QString& device_id, const QString& path, const QJsonObject& params,
                    QObject* context, Answer done, bool retried = false);
    void requestDevices(QObject* context, Callback<QList<Device>> done, bool retried);
    void reconnect(QObject* context, std::function<void(bool)> done);
    void adoptSession(const QJsonObject& answer);
    [[nodiscard]] qint64 nextRequestId();

    QNetworkAccessManager& network_;
    QUrl endpoint_;
    Secrets secrets_;
    QString session_token_;
    QString regain_token_;
    QByteArray server_token_;
    QByteArray device_token_;
    qint64 last_rid_ = 0;
};

} // namespace animehub::downloads::myjd

#endif // ANIMEHUB_CORE_DOWNLOADS_MYJDOWNLOADER_H
