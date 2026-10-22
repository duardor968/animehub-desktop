#include "downloads/myjdownloader.h"
#include "http_test_server.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>
#include <QUrlQuery>

using namespace animehub::downloads;
using animehub::testing::HttpTestServer;

namespace {

QJsonObject vectors() {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/myjd/vectors.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing MyJDownloader vectors");
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray hex(const QJsonObject& v, const char* key) {
    return QByteArray::fromHex(v.value(QLatin1StringView(key)).toString().toLatin1());
}

// A stand-in for api.jdownloader.org that checks signatures and speaks the encrypted protocol.
class FakeMyJd {
public:
    myjd::Secrets secrets = myjd::Secrets::derive(QStringLiteral("duardo@example.test"),
                                                  QStringLiteral("contraseña de prueba"));
    QString session = QStringLiteral("0011223344556677");
    bool expireOnce = false;
    QString lastLinks;
    QString lastPackage;
    int reconnects = 0;

    void install(HttpTestServer& server) {
        server.route("POST", "/my/connect", [this](const HttpTestServer::Request& r) {
            if (!signedWith(r, secrets.login)) {
                return HttpTestServer::Response{.status = 403, .body = kAuthFailed};
            }
            return reply(secrets.login, rid(r),
                         {{QStringLiteral("sessiontoken"), session},
                          {QStringLiteral("regaintoken"), QStringLiteral("aa")}});
        });
        server.route("POST", "/my/reconnect", [this](const HttpTestServer::Request& r) {
            const auto old_server = myjd::updateToken(secrets.login, session);
            if (!signedWith(r, old_server)) {
                return HttpTestServer::Response{.status = 403, .body = kAuthFailed};
            }
            ++reconnects;
            session = QStringLiteral("8899aabbccddeeff");
            server_base = old_server;
            return reply(old_server, rid(r),
                         {{QStringLiteral("sessiontoken"), session},
                          {QStringLiteral("regaintoken"), QStringLiteral("bb")}});
        });
        server.route("POST", "/my/listdevices", [this](const HttpTestServer::Request& r) {
            if (expireOnce) {
                expireOnce = false;
                return HttpTestServer::Response{.status = 403, .body = kTokenInvalid};
            }
            const auto key = serverToken();
            if (!signedWith(r, key)) {
                return HttpTestServer::Response{.status = 403, .body = kAuthFailed};
            }
            const QJsonArray list{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("dev1")},
                            {QStringLiteral("name"), QStringLiteral("PC de Duardo")}}};
            return reply(key, rid(r), {{QStringLiteral("list"), list}});
        });
    }

    void installDevice(HttpTestServer& server) {
        server.route("POST", "/t_" + session.toLatin1() + "_dev1/linkgrabberv2/addLinks",
                     [this](const HttpTestServer::Request& r) {
                         const auto key = myjd::updateToken(secrets.device, session);
                         const auto plain = myjd::decrypt(r.body, key);
                         const auto call = QJsonDocument::fromJson(plain.value_or("")).object();
                         const auto params =
                             QJsonDocument::fromJson(
                                 call.value(u"params").toArray().first().toString().toUtf8())
                                 .object();
                         lastLinks = params.value(u"links").toString();
                         lastPackage = params.value(u"packageName").toString();
                         return reply(key, call.value(u"rid").toInteger(),
                                      {{QStringLiteral("data"), QJsonValue::Null}});
                     });
    }

private:
    static inline const QByteArray kAuthFailed = "{\"src\":\"MYJD\",\"type\":\"AUTH_FAILED\"}";
    static inline const QByteArray kTokenInvalid = "{\"src\":\"MYJD\",\"type\":\"TOKEN_INVALID\"}";
    QByteArray server_base;

    QByteArray serverToken() const {
        return myjd::updateToken(server_base.isEmpty() ? secrets.login : server_base, session);
    }

    static qint64 rid(const HttpTestServer::Request& r) {
        return QUrlQuery(QString::fromLatin1(r.query))
            .queryItemValue(QStringLiteral("rid"))
            .toLongLong();
    }

    static bool signedWith(const HttpTestServer::Request& r, const QByteArray& key) {
        const auto at = r.query.lastIndexOf("&signature=");
        if (at < 0) {
            return false;
        }
        const auto signed_part = r.path + '?' + r.query.left(at);
        return myjd::sign(key, signed_part).toLatin1() == r.query.mid(at + 11);
    }

    static HttpTestServer::Response reply(const QByteArray& key, qint64 rid, QJsonObject object) {
        object.insert(QStringLiteral("rid"), rid);
        return {.body = myjd::encrypt(QJsonDocument(object).toJson(QJsonDocument::Compact), key)};
    }
};

} // namespace

class MyJdTest final : public QObject {
    Q_OBJECT

private slots:
    void matchesTheReferenceImplementation() {
        const auto v = vectors();
        const auto secrets =
            myjd::Secrets::derive(v.value(u"email").toString(), v.value(u"password").toString());
        QCOMPARE(secrets.login, hex(v, "login"));
        QCOMPARE(secrets.device, hex(v, "device"));
        const auto session = v.value(u"session").toString();
        QCOMPARE(myjd::updateToken(secrets.login, session), hex(v, "serverToken"));
        QCOMPARE(myjd::updateToken(secrets.device, session), hex(v, "deviceToken"));
        QCOMPARE(myjd::sign(secrets.login, v.value(u"path").toString().toUtf8()),
                 v.value(u"signature").toString());
        const auto payload = v.value(u"payload").toString().toUtf8();
        QCOMPARE(myjd::encrypt(payload, hex(v, "deviceToken")),
                 v.value(u"cipher").toString().toLatin1());
        QCOMPARE(myjd::decrypt(v.value(u"cipher").toString().toLatin1(), hex(v, "deviceToken")),
                 payload);
    }

    void derivesFromTheLowerCasedAddress() {
        const auto a =
            myjd::Secrets::derive(QStringLiteral(" Duardo@Example.TEST "), QStringLiteral("x"));
        const auto b =
            myjd::Secrets::derive(QStringLiteral("duardo@example.test"), QStringLiteral("x"));
        QCOMPARE(a.login, b.login);
        QCOMPARE(a.email, QStringLiteral("duardo@example.test"));
        const auto restored = myjd::Secrets::deserialize(a.email, a.serialize());
        QVERIFY(restored);
        QCOMPARE(restored->device, a.device);
        QVERIFY(!myjd::Secrets::deserialize(a.email, "short"));
    }

    void rejectsDamagedCiphertext() {
        const auto key = QByteArray(32, 'k');
        QVERIFY(!myjd::decrypt("not base64 at all", key));
        QVERIFY(!myjd::decrypt(QByteArray(15, 'A').toBase64(), key));
        auto good = myjd::encrypt("hola", key);
        auto raw = QByteArray::fromBase64(good);
        raw[raw.size() - 1] = static_cast<char>(raw[raw.size() - 1] ^ 0x55); // breaks the padding
        QVERIFY(!myjd::decrypt(raw.toBase64(), key));
    }

    void connectsListsDevicesAndAddsLinks() {
        HttpTestServer server;
        QVERIFY(server.listen());
        FakeMyJd fake;
        fake.install(server);
        QNetworkAccessManager network;
        myjd::Client client(network, nullptr,
                            server.url(QString()).adjusted(QUrl::StripTrailingSlash));

        std::optional<std::expected<void, myjd::Failure>> connected;
        client.connectAccount(fake.secrets, this, [&](const auto& r) { connected = r; });
        QTRY_VERIFY(connected.has_value());
        QVERIFY(*connected);

        std::optional<std::expected<QList<myjd::Device>, myjd::Failure>> devices;
        client.devices(this, [&](const auto& r) { devices = r; });
        QTRY_VERIFY(devices.has_value());
        QVERIFY(*devices);
        QCOMPARE((*devices)->first().name, QStringLiteral("PC de Duardo"));

        fake.installDevice(server);
        std::optional<std::expected<void, myjd::Failure>> added;
        client.addLinks(QStringLiteral("dev1"), QStringLiteral("Sousou no Frieren"),
                        {QUrl(QStringLiteral("https://mega.nz/file/a#b")),
                         QUrl(QStringLiteral("https://www.mp4upload.com/x"))},
                        this, [&](const auto& r) { added = r; });
        QTRY_VERIFY(added.has_value());
        QVERIFY(*added);
        QCOMPARE(fake.lastPackage, QStringLiteral("Sousou no Frieren"));
        QCOMPARE(fake.lastLinks.split(u'\n').size(), 2);
    }

    void scopedDeviceCallsPreservePositionalEncryptedArguments() {
        HttpTestServer server;
        QVERIFY(server.listen());
        FakeMyJd fake;
        fake.install(server);
        QNetworkAccessManager network;
        myjd::Client client(network, nullptr,
                            server.url(QString()).adjusted(QUrl::StripTrailingSlash));
        bool ready = false;
        client.connectAccount(fake.secrets, this,
                              [&](const auto& result) { ready = result.has_value(); });
        QTRY_VERIFY(ready);
        QJsonArray arguments;
        server.route("POST", "/t_" + fake.session.toLatin1() + "_dev1/downloadsV2/forceDownload",
                     [&](const auto& request) {
                         const auto key = myjd::updateToken(fake.secrets.device, fake.session);
                         const auto plain = myjd::decrypt(request.body, key);
                         const auto payload = QJsonDocument::fromJson(plain.value_or("")).object();
                         arguments = payload.value(u"params").toArray();
                         const QJsonObject response{{QStringLiteral("rid"), payload.value(u"rid")},
                                                    {QStringLiteral("data"), QJsonValue::Null}};
                         return HttpTestServer::Response{
                             .body = myjd::encrypt(QJsonDocument(response).toJson(), key)};
                     });
        std::optional<std::expected<QJsonValue, myjd::Failure>> response;
        client.request(QStringLiteral("dev1"), QStringLiteral("/downloadsV2/forceDownload"),
                       {QJsonArray{qint64(9007199254740993LL)}, QJsonArray{}}, this,
                       [&](const auto& result) { response = result; });
        QTRY_VERIFY(response.has_value());
        QVERIFY(*response);
        QCOMPARE(arguments.size(), 2);
        QCOMPARE(arguments[0].toString(), QStringLiteral("[9007199254740993]"));
        QCOMPARE(arguments[1].toString(), QStringLiteral("[]"));
    }

    void reconnectsWhenTheSessionExpires() {
        HttpTestServer server;
        QVERIFY(server.listen());
        FakeMyJd fake;
        fake.install(server);
        QNetworkAccessManager network;
        myjd::Client client(network, nullptr,
                            server.url(QString()).adjusted(QUrl::StripTrailingSlash));
        bool ready = false;
        client.connectAccount(fake.secrets, this, [&](const auto&) { ready = true; });
        QTRY_VERIFY(ready);

        fake.expireOnce = true;
        std::optional<std::expected<QList<myjd::Device>, myjd::Failure>> devices;
        client.devices(this, [&](const auto& r) { devices = r; });
        QTRY_VERIFY(devices.has_value());
        QVERIFY(*devices);
        QCOMPARE(fake.reconnects, 1);
    }

    void reportsWrongCredentials() {
        HttpTestServer server;
        QVERIFY(server.listen());
        FakeMyJd fake;
        fake.install(server);
        QNetworkAccessManager network;
        myjd::Client client(network, nullptr,
                            server.url(QString()).adjusted(QUrl::StripTrailingSlash));
        std::optional<std::expected<void, myjd::Failure>> connected;
        client.connectAccount(
            myjd::Secrets::derive(QStringLiteral("duardo@example.test"), QStringLiteral("otra")),
            this, [&](const auto& r) { connected = r; });
        QTRY_VERIFY(connected.has_value());
        QVERIFY(!*connected);
        QCOMPARE(connected->error(), myjd::Failure::WrongCredentials);
        QVERIFY(!client.isConnected());
    }
};

QTEST_GUILESS_MAIN(MyJdTest)
#include "myjd_test.moc"
