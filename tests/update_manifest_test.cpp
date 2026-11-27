#include "monocypher-ed25519.h"
#include "update/update_keys.h"
#include "update/update_manifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <array>
#include <cstring>

using namespace animehub::update;

namespace {

struct EphemeralKeyPair {
    std::array<std::uint8_t, 64> secret{};
    Ed25519PublicKey pub{};
};

EphemeralKeyPair makeKeyPair(std::uint8_t seed_fill) {
    std::array<std::uint8_t, 32> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] = static_cast<std::uint8_t>(seed_fill ^ static_cast<std::uint8_t>(i * 13 + 7));
    }
    EphemeralKeyPair kp;
    crypto_ed25519_key_pair(kp.secret.data(), kp.pub.data(), seed.data());
    return kp;
}

QByteArray signEnvelope(const QJsonObject& payload_obj, const QString& key_id,
                        const EphemeralKeyPair& kp) {
    const QByteArray raw_payload = QJsonDocument(payload_obj).toJson(QJsonDocument::Compact);
    QByteArray msg;
    msg.append(kManifestDomainPrefix.data(), static_cast<qsizetype>(kManifestDomainPrefix.size()));
    msg.append(raw_payload);
    Ed25519Signature sig{};
    crypto_ed25519_sign(sig.data(), kp.secret.data(),
                        reinterpret_cast<const std::uint8_t*>(msg.constData()),
                        static_cast<std::size_t>(msg.size()));
    const QByteArray sig_bytes(reinterpret_cast<const char*>(sig.data()),
                               static_cast<qsizetype>(sig.size()));
    const QJsonObject env{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("keyId"), key_id},
        {QStringLiteral("signature"), QString::fromLatin1(sig_bytes.toBase64())},
        {QStringLiteral("payload"), QString::fromLatin1(raw_payload.toBase64())},
    };
    return QJsonDocument(env).toJson(QJsonDocument::Compact);
}

QString validSha(char fill = 'a') {
    return QString(64, QLatin1Char(fill));
}

QJsonObject validWin64Payload(const QString& version = QStringLiteral("0.5.0"), int seq = 5) {
    return QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("platform"), QStringLiteral("win64")},
        {QStringLiteral("channel"), QStringLiteral("stable")},
        {QStringLiteral("version"), version},
        {QStringLiteral("releaseSeq"), seq},
        {QStringLiteral("isMajor"), false},
        {QStringLiteral("minUpdaterProtocol"), 1},
        {QStringLiteral("requiresFullInstaller"), false},
        {QStringLiteral("notes"), QStringLiteral("Bug fixes and speedups")},
        {QStringLiteral("releaseUrl"),
         QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/tag/v0.5.0")},
        {QStringLiteral("pack"),
         QJsonObject{
             {QStringLiteral("filename"), QStringLiteral("AnimeHub-0.5.0-win64.ahpack")},
             {QStringLiteral("size"), 4096},
             {QStringLiteral("sha256"), validSha('1')},
             {QStringLiteral("urls"),
              QJsonArray{QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/"
                                        "download/v0.5.0/AnimeHub-0.5.0-win64.ahpack")}},
         }},
        {QStringLiteral("files"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("AnimeHub.exe")},
                 {QStringLiteral("kind"), QStringLiteral("file")},
                 {QStringLiteral("encoding"), QStringLiteral("zstd")},
                 {QStringLiteral("size"), 1024},
                 {QStringLiteral("sha256"), validSha('2')},
                 {QStringLiteral("packOffset"), 0},
                 {QStringLiteral("packSize"), 512},
                 {QStringLiteral("group"), QStringLiteral("app")},
                 {QStringLiteral("executable"), true},
             },
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("qml")},
                 {QStringLiteral("kind"), QStringLiteral("solid-tar")},
                 {QStringLiteral("encoding"), QStringLiteral("tar+zstd")},
                 {QStringLiteral("size"), 2048},
                 {QStringLiteral("sha256"), validSha('3')},
                 {QStringLiteral("packOffset"), 512},
                 {QStringLiteral("packSize"), 400},
                 {QStringLiteral("group"), QStringLiteral("qml")},
                 {QStringLiteral("members"),
                  QJsonArray{QJsonObject{
                      {QStringLiteral("path"), QStringLiteral("qml/Main.qml")},
                      {QStringLiteral("size"), 100},
                      {QStringLiteral("sha256"), validSha('4')},
                  }}},
             },
         }},
    };
}

} // namespace

class UpdateManifestTest final : public QObject {
    Q_OBJECT

private slots:
    void validSignatureAndSchema() {
        const auto rel = makeKeyPair(0x11);
        const auto rec = makeKeyPair(0x22);
        const KeyRing ring{.keys = {
                               TrustedKey{.id = QStringLiteral("release-test"),
                                          .publicKey = rel.pub,
                                          .isRecovery = false},
                               TrustedKey{.id = QStringLiteral("recovery-test"),
                                          .publicKey = rec.pub,
                                          .isRecovery = true},
                           }};

        const QByteArray env =
            signEnvelope(validWin64Payload(), QStringLiteral("release-test"), rel);
        const auto parsed =
            verifyAndParseManifestEnvelope(env, ring, {}, QStringLiteral("win64"), UrlPolicy{});
        QVERIFY2(parsed.has_value(), parsed.has_value() ? "" : qPrintable(parsed.error().message));
        QCOMPARE(parsed->version, (SemVer{0, 5, 0}));
        QCOMPARE(parsed->releaseSeq, 5ULL);
        QCOMPARE(parsed->verifiedKeyId, QStringLiteral("release-test"));
        QVERIFY(!parsed->verifiedByRecoveryKey);
        QVERIFY(parsed->pack.has_value());
        QCOMPARE(parsed->files.size(), 2);
        QCOMPARE(parsed->files[1].members.size(), 1);
    }

    void tamperedPayloadRejected() {
        const auto rel = makeKeyPair(0x31);
        const KeyRing ring{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                               .publicKey = rel.pub,
                                               .isRecovery = false}}};
        const QByteArray env =
            signEnvelope(validWin64Payload(), QStringLiteral("release-test"), rel);
        QJsonObject obj = QJsonDocument::fromJson(env).object();
        QByteArray raw =
            QByteArray::fromBase64(obj.value(QStringLiteral("payload")).toString().toLatin1());
        raw.replace("0.5.0", "0.5.1");
        obj.insert(QStringLiteral("payload"), QString::fromLatin1(raw.toBase64()));

        const auto res =
            verifyAndParseManifestEnvelope(QJsonDocument(obj).toJson(QJsonDocument::Compact), ring,
                                           {}, QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(!res.has_value());
        QCOMPARE(res.error().code, QStringLiteral("invalid_signature"));
    }

    void wrongKeyRejected() {
        const auto trusted = makeKeyPair(0x41);
        const auto attacker = makeKeyPair(0x42);
        const KeyRing ring{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                               .publicKey = trusted.pub,
                                               .isRecovery = false}}};

        // Signed by attacker claiming to be release-test.
        const QByteArray forged =
            signEnvelope(validWin64Payload(), QStringLiteral("release-test"), attacker);
        const auto res1 =
            verifyAndParseManifestEnvelope(forged, ring, {}, QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(!res1.has_value());
        QCOMPARE(res1.error().code, QStringLiteral("invalid_signature"));

        // Signed with unknown key ID.
        const QByteArray unknown =
            signEnvelope(validWin64Payload(), QStringLiteral("evil-key"), attacker);
        const auto res2 =
            verifyAndParseManifestEnvelope(unknown, ring, {}, QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(!res2.has_value());
        QCOMPARE(res2.error().code, QStringLiteral("unknown_key"));
    }

    void recoveryKeyRevokesReleaseKey() {
        const auto rel = makeKeyPair(0x51);
        const auto rec = makeKeyPair(0x52);
        const KeyRing ring{.keys = {
                               TrustedKey{.id = QStringLiteral("release-test"),
                                          .publicKey = rel.pub,
                                          .isRecovery = false},
                               TrustedKey{.id = QStringLiteral("recovery-test"),
                                          .publicKey = rec.pub,
                                          .isRecovery = true},
                           }};

        // Release key CANNOT revoke the recovery key.
        QJsonObject attempt = validWin64Payload();
        attempt.insert(QStringLiteral("revokedKeyIds"),
                       QJsonArray{QStringLiteral("recovery-test")});
        const auto parsed_rel = verifyAndParseManifestEnvelope(
            signEnvelope(attempt, QStringLiteral("release-test"), rel), ring, {},
            QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(parsed_rel.has_value());
        QVERIFY(parsed_rel->revokedKeyIds.isEmpty());

        // Recovery key revokes release-test.
        QJsonObject revoke_payload = validWin64Payload(QStringLiteral("0.5.1"), 6);
        revoke_payload.insert(QStringLiteral("revokedKeyIds"),
                              QJsonArray{QStringLiteral("release-test")});
        const auto parsed_rec = verifyAndParseManifestEnvelope(
            signEnvelope(revoke_payload, QStringLiteral("recovery-test"), rec), ring, {},
            QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(parsed_rec.has_value());
        QVERIFY(parsed_rec->verifiedByRecoveryKey);
        QCOMPARE(parsed_rec->revokedKeyIds, QStringList{QStringLiteral("release-test")});

        // Subsequent manifest signed by revoked release-test MUST be rejected!
        const QSet<QString> revoked{QStringLiteral("release-test")};
        const auto blocked = verifyAndParseManifestEnvelope(
            signEnvelope(validWin64Payload(QStringLiteral("0.6.0"), 7),
                         QStringLiteral("release-test"), rel),
            ring, revoked, QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(!blocked.has_value());
        QCOMPARE(blocked.error().code, QStringLiteral("key_revoked"));
    }

    void downgradeRejected() {
        UpdateManifest m;
        m.version = SemVer{0, 4, 0};
        m.releaseSeq = 4;

        // Lower semver rejected.
        const auto r1 = checkUpgradeEligibility(m, SemVer{0, 5, 0}, 5);
        QVERIFY(!r1.has_value());
        QCOMPARE(r1.error().code, QStringLiteral("downgrade_version"));

        // Equal semver & equal seq -> up to date (false, not error).
        const auto r2 = checkUpgradeEligibility(m, SemVer{0, 4, 0}, 4);
        QVERIFY(r2.has_value());
        QCOMPARE(*r2, false);

        // Higher semver BUT lower/equal releaseSeq -> rejected!
        m.version = SemVer{0, 5, 0};
        m.releaseSeq = 4;
        const auto r3 = checkUpgradeEligibility(m, SemVer{0, 4, 0}, 4);
        QVERIFY(!r3.has_value());
        QCOMPARE(r3.error().code, QStringLiteral("downgrade_seq"));

        // Higher semver AND higher releaseSeq -> accepted!
        m.releaseSeq = 5;
        const auto r4 = checkUpgradeEligibility(m, SemVer{0, 4, 0}, 4);
        QVERIFY(r4.has_value());
        QCOMPARE(*r4, true);
        QVERIFY(!(SemVer{0, 5, 0}.isMajorJumpFrom(SemVer{0, 4, 0})));
        QVERIFY((SemVer{1, 0, 0}.isMajorJumpFrom(SemVer{0, 4, 0})));
    }

    void unsafePathsRejected() {
        const QStringList bad_paths = {
            QString(),
            QStringLiteral("../evil.dll"),
            QStringLiteral("a/../../evil.dll"),
            QStringLiteral("/etc/passwd"),
            QStringLiteral("C:\\Windows\\evil.exe"),
            QStringLiteral("C:/Windows/evil.exe"),
            QStringLiteral("AnimeHub.exe:zone.identifier"),
            QStringLiteral("sub\\file.dll"),
            QStringLiteral(".update/plan.json"),
            QStringLiteral("qml/CON.txt"),
            QStringLiteral("qml/trailing."),
        };
        for (const QString& p : bad_paths) {
            QVERIFY2(!isSafeRelativeUpdatePath(p), qPrintable(p));
        }
        QVERIFY(isSafeRelativeUpdatePath(QStringLiteral("AnimeHub.exe")));
        QVERIFY(isSafeRelativeUpdatePath(QStringLiteral("qml/AnimeHub/Main.qml")));
        QVERIFY(isSafeRelativeUpdatePath(QStringLiteral("engines/mpv/mpv.exe")));

        // Verify manifest parser rejects unsafe path inside files[] too.
        const auto rel = makeKeyPair(0x61);
        const KeyRing ring{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                               .publicKey = rel.pub,
                                               .isRecovery = false}}};
        QJsonObject payload = validWin64Payload();
        QJsonArray files = payload.value(QStringLiteral("files")).toArray();
        QJsonObject bad = files[0].toObject();
        bad.insert(QStringLiteral("path"), QStringLiteral("../escape.exe"));
        files[0] = bad;
        payload.insert(QStringLiteral("files"), files);

        const auto res = verifyAndParseManifestEnvelope(
            signEnvelope(payload, QStringLiteral("release-test"), rel), ring, {},
            QStringLiteral("win64"), UrlPolicy{});
        QVERIFY(!res.has_value());
        QCOMPARE(res.error().code, QStringLiteral("unsafe_path"));
    }

    void urlPolicyAndApiWrapper() {
        const UrlPolicy prod;
        QVERIFY(isAllowedUpdateUrl(
            QUrl(QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/latest")),
            prod));
        QVERIFY(isAllowedUpdateUrl(
            QUrl(QStringLiteral("https://objects.githubusercontent.com/foo/bar")), prod));
        QVERIFY(
            isAllowedUpdateUrl(QUrl(QStringLiteral("https://s3.duardo.dev/bucket/file")), prod));
        QVERIFY(!isAllowedUpdateUrl(QUrl(QStringLiteral("http://github.com/foo")), prod));
        QVERIFY(
            !isAllowedUpdateUrl(QUrl(QStringLiteral("https://evil.example.com/a.ahpack")), prod));
        QVERIFY(!isAllowedUpdateUrl(QUrl(QStringLiteral("https://evilduardo.dev/a.ahpack")), prod));
        QVERIFY(!isAllowedUpdateUrl(QUrl(QStringLiteral("http://127.0.0.1:8080/a")), prod));

        UrlPolicy local = prod;
        local.allowInsecureLoopback = true;
        QVERIFY(isAllowedUpdateUrl(QUrl(QStringLiteral("http://127.0.0.1:8080/a")), local));

        // API wrapper with paused=true and extra mirror URL.
        const QByteArray paused_json =
            R"({"channel":"stable","paused":true,"minSupportedVersion":"0.3.0"})";
        const auto paused = parseApiReleaseResponse(paused_json, QStringLiteral("win64"), prod);
        QVERIFY(paused.has_value());
        QVERIFY(paused->paused);
        QCOMPARE(paused->minSupportedVersion, std::optional<SemVer>(SemVer{0, 3, 0}));

        const auto rel = makeKeyPair(0x71);
        const KeyRing ring{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                               .publicKey = rel.pub,
                                               .isRecovery = false}}};
        const QByteArray env_bytes =
            signEnvelope(validWin64Payload(), QStringLiteral("release-test"), rel);
        const QJsonObject api_obj{
            {QStringLiteral("channel"), QStringLiteral("stable")},
            {QStringLiteral("paused"), false},
            {QStringLiteral("manifests"),
             QJsonObject{{QStringLiteral("win64"),
                          QJsonObject{{QStringLiteral("envelope"),
                                       QJsonDocument::fromJson(env_bytes).object()}}}}},
            {QStringLiteral("assets"),
             QJsonObject{
                 {QStringLiteral("AnimeHub-0.5.0-win64.ahpack"),
                  QJsonObject{
                      {QStringLiteral("urls"),
                       QJsonArray{QStringLiteral("https://s3.duardo.dev/releases/pack.ahpack"),
                                  QStringLiteral("https://evil.example.com/spoof.ahpack")}}}},
             }},
        };
        const auto parsed_api = parseApiReleaseResponse(
            QJsonDocument(api_obj).toJson(QJsonDocument::Compact), QStringLiteral("win64"), prod);
        QVERIFY(parsed_api.has_value());
        auto manifest = verifyAndParseManifestEnvelope(parsed_api->inlineEnvelopeJson, ring, {},
                                                       QStringLiteral("win64"), prod);
        QVERIFY(manifest.has_value());
        mergeExtraMirrorUrls(*manifest, parsed_api->extraAssetUrls, prod);
        QCOMPARE(manifest->pack->urls.size(), 2);
        QCOMPARE(manifest->pack->urls[1],
                 QUrl(QStringLiteral("https://s3.duardo.dev/releases/pack.ahpack")));
    }
};

QTEST_GUILESS_MAIN(UpdateManifestTest)
#include "update_manifest_test.moc"
