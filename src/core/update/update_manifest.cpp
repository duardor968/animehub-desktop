#include "update_manifest.h"

#include "monocypher-ed25519.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cstring>

namespace animehub::update {
namespace {

bool isReservedWindowsName(const QString& segment) {
    const qsizetype dot = segment.indexOf(QLatin1Char('.'));
    const QString stem = (dot >= 0 ? segment.left(dot) : segment).toUpper();
    if (stem == QLatin1StringView("CON") || stem == QLatin1StringView("PRN") ||
        stem == QLatin1StringView("AUX") || stem == QLatin1StringView("NUL")) {
        return true;
    }
    if (stem.size() == 4 &&
        (stem.startsWith(QLatin1StringView("COM")) || stem.startsWith(QLatin1StringView("LPT")))) {
        const QChar digit = stem.at(3);
        return digit >= QLatin1Char('1') && digit <= QLatin1Char('9');
    }
    return false;
}

bool hostMatchesRule(const QString& host, const QString& rule) {
    const QString h = host.toLower();
    const QString r = rule.toLower();
    if (r.startsWith(QLatin1StringView("*."))) {
        const QString suffix = r.mid(1); // ".duardo.dev"
        return h.size() > suffix.size() && h.endsWith(suffix);
    }
    return h == r;
}

std::optional<Ed25519Signature> decodeSignature(const QString& text) {
    const QString trimmed = text.trimmed();
    QByteArray raw;
    if (trimmed.size() == 128) {
        const QByteArray hex = trimmed.toLatin1();
        bool all_hex = true;
        for (char c : hex) {
            const bool is_hex =
                (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!is_hex) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            raw = QByteArray::fromHex(hex);
        }
    }
    if (raw.isEmpty()) {
        auto decoded = QByteArray::fromBase64Encoding(trimmed.toLatin1(),
                                                      QByteArray::Base64Encoding |
                                                          QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            return std::nullopt;
        }
        raw = decoded.decoded;
    }
    if (raw.size() != 64) {
        return std::nullopt;
    }
    Ed25519Signature sig{};
    std::memcpy(sig.data(), raw.constData(), sig.size());
    return sig;
}

std::optional<std::uint64_t> parseUnsignedInt64(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double d = value.toDouble(-1.0);
    if (d < 0.0 || d > 9007199254740991.0) {
        return std::nullopt;
    }
    const auto integer = static_cast<std::uint64_t>(d);
    if (static_cast<double>(integer) != d) {
        return std::nullopt;
    }
    return integer;
}

QList<QUrl> parseUrlList(const QJsonObject& obj, const QString& array_key,
                         const QString& single_key, const UrlPolicy& policy, bool& any_invalid) {
    QList<QUrl> urls;
    const QJsonValue arr_val = obj.value(array_key);
    if (arr_val.isArray()) {
        for (const QJsonValue& item : arr_val.toArray()) {
            if (!item.isString()) {
                any_invalid = true;
                continue;
            }
            const QUrl url(item.toString().trimmed(), QUrl::StrictMode);
            if (!isAllowedUpdateUrl(url, policy)) {
                any_invalid = true;
                continue;
            }
            if (!urls.contains(url)) {
                urls.append(url);
            }
        }
    } else if (!single_key.isEmpty() && obj.value(single_key).isString()) {
        const QUrl url(obj.value(single_key).toString().trimmed(), QUrl::StrictMode);
        if (!isAllowedUpdateUrl(url, policy)) {
            any_invalid = true;
        } else {
            urls.append(url);
        }
    }
    return urls;
}

std::expected<std::optional<UpdateArtifact>, UpdateError>
parseOptionalArtifact(const QJsonObject& root, const QString& key, const UrlPolicy& policy) {
    const QJsonValue val = root.value(key);
    if (val.isUndefined() || val.isNull()) {
        return std::optional<UpdateArtifact>{};
    }
    if (!val.isObject()) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_schema"),
                        QStringLiteral("Artifact '%1' must be an object").arg(key)});
    }
    const QJsonObject obj = val.toObject();
    UpdateArtifact artifact;
    artifact.filename = obj.value(QStringLiteral("filename")).toString().trimmed();
    if (artifact.filename.isEmpty() || artifact.filename.contains(QLatin1Char('/')) ||
        artifact.filename.contains(QLatin1Char('\\')) ||
        artifact.filename.contains(QLatin1StringView(".."))) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_schema"),
                        QStringLiteral("Artifact '%1' has invalid filename").arg(key)});
    }
    const auto size = parseUnsignedInt64(obj.value(QStringLiteral("size")));
    if (!size || *size == 0) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_schema"),
                        QStringLiteral("Artifact '%1' has invalid size").arg(key)});
    }
    artifact.size = static_cast<qint64>(*size);
    artifact.sha256 = normalizeSha256Hex(obj.value(QStringLiteral("sha256")).toString());
    if (artifact.sha256.isEmpty()) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_schema"),
                        QStringLiteral("Artifact '%1' has invalid sha256").arg(key)});
    }
    bool any_invalid = false;
    artifact.urls =
        parseUrlList(obj, QStringLiteral("urls"), QStringLiteral("url"), policy, any_invalid);
    if (artifact.urls.isEmpty()) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_url"),
                        QStringLiteral("Artifact '%1' has no allowed HTTPS URL").arg(key)});
    }
    bool zsync_invalid = false;
    artifact.zsyncUrls = parseUrlList(obj, QStringLiteral("zsyncUrls"), QStringLiteral("zsyncUrl"),
                                      policy, zsync_invalid);
    return artifact;
}

void appendUniqueAllowedUrls(QList<QUrl>& target, const QList<QUrl>& extra,
                             const UrlPolicy& policy) {
    for (const QUrl& url : extra) {
        if (isAllowedUpdateUrl(url, policy) && !target.contains(url)) {
            target.append(url);
        }
    }
}

} // namespace

bool isSafeRelativeUpdatePath(const QString& path) {
    if (path.isEmpty() || path.size() > 512) {
        return false;
    }
    if (path.startsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('/'))) {
        return false;
    }
    for (const QChar ch : path) {
        const ushort code = ch.unicode();
        if (code < 0x20 || code == 0x7F || ch == QLatin1Char('\\') || ch == QLatin1Char(':') ||
            ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('"') ||
            ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('|')) {
            return false;
        }
    }
    const QStringList segments = path.split(QLatin1Char('/'));
    if (segments.isEmpty()) {
        return false;
    }
    if (segments.first().compare(QLatin1StringView(".update"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    for (const QString& segment : segments) {
        if (segment.isEmpty() || segment == QLatin1StringView(".") ||
            segment == QLatin1StringView("..") || segment.endsWith(QLatin1Char(' ')) ||
            segment.endsWith(QLatin1Char('.'))) {
            return false;
        }
        if (isReservedWindowsName(segment)) {
            return false;
        }
    }
    return true;
}

bool isAllowedUpdateUrl(const QUrl& url, const UrlPolicy& policy) {
    if (!url.isValid() || url.isRelative() || !url.userInfo().isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    const QString host = url.host().toLower();
    if (host.isEmpty()) {
        return false;
    }
    if (scheme == QLatin1StringView("http")) {
        if (!policy.allowInsecureLoopback) {
            return false;
        }
        return host == QLatin1StringView("127.0.0.1") || host == QLatin1StringView("localhost") ||
               host == QLatin1StringView("::1");
    }
    if (scheme != QLatin1StringView("https")) {
        return false;
    }
    for (const QString& rule : policy.allowedHosts) {
        if (hostMatchesRule(host, rule)) {
            return true;
        }
    }
    return false;
}

QByteArray normalizeSha256Hex(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() != 64) {
        return {};
    }
    QByteArray result(64, Qt::Uninitialized);
    for (int i = 0; i < 64; ++i) {
        const ushort code = trimmed.at(i).unicode();
        if (code >= '0' && code <= '9') {
            result[i] = static_cast<char>(code);
        } else if (code >= 'a' && code <= 'f') {
            result[i] = static_cast<char>(code);
        } else if (code >= 'A' && code <= 'F') {
            result[i] = static_cast<char>(code - 'A' + 'a');
        } else {
            return {};
        }
    }
    return result;
}

bool verifyManifestSignature(const QByteArray& raw_payload, const Ed25519Signature& signature,
                             const Ed25519PublicKey& public_key) {
    QByteArray message;
    message.reserve(static_cast<qsizetype>(kManifestDomainPrefix.size()) + raw_payload.size());
    message.append(kManifestDomainPrefix.data(),
                   static_cast<qsizetype>(kManifestDomainPrefix.size()));
    message.append(raw_payload);
    return crypto_ed25519_check(signature.data(), public_key.data(),
                                reinterpret_cast<const std::uint8_t*>(message.constData()),
                                static_cast<std::size_t>(message.size())) == 0;
}

std::expected<UpdateManifest, UpdateError>
verifyAndParseManifestEnvelope(const QByteArray& envelope_json, const KeyRing& key_ring,
                               const QSet<QString>& revoked_key_ids,
                               const QString& expected_platform, const UrlPolicy& url_policy) {
    QJsonParseError parse_error{};
    const QJsonDocument envelope_doc = QJsonDocument::fromJson(envelope_json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !envelope_doc.isObject()) {
        return std::unexpected(
            UpdateError{QStringLiteral("envelope_json"),
                        QStringLiteral("Invalid update manifest envelope JSON")});
    }
    const QJsonObject envelope = envelope_doc.object();
    if (envelope.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        return std::unexpected(
            UpdateError{QStringLiteral("envelope_schema"),
                        QStringLiteral("Unsupported update manifest envelope schemaVersion")});
    }

    const QJsonValue payload_val = envelope.value(QStringLiteral("payload"));
    if (!payload_val.isString()) {
        return std::unexpected(
            UpdateError{QStringLiteral("envelope_payload"),
                        QStringLiteral("Manifest envelope is missing base64 payload")});
    }
    const auto decoded_payload = QByteArray::fromBase64Encoding(
        payload_val.toString().trimmed().toLatin1(),
        QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded_payload || decoded_payload.decoded.isEmpty()) {
        return std::unexpected(
            UpdateError{QStringLiteral("envelope_payload"),
                        QStringLiteral("Manifest envelope has invalid base64 payload")});
    }
    const QByteArray& raw_payload = decoded_payload.decoded;

    struct CandidateSig {
        QString keyId;
        QString sigText;
    };
    QList<CandidateSig> candidates;
    if (envelope.value(QStringLiteral("signatures")).isArray()) {
        for (const QJsonValue& item : envelope.value(QStringLiteral("signatures")).toArray()) {
            const QJsonObject obj = item.toObject();
            const QString id = obj.value(QStringLiteral("keyId")).toString().trimmed();
            const QString sig = obj.value(QStringLiteral("signature")).toString().trimmed();
            if (!id.isEmpty() && !sig.isEmpty()) {
                candidates.append(CandidateSig{.keyId = id, .sigText = sig});
            }
        }
    }
    const QString top_key_id = envelope.value(QStringLiteral("keyId")).toString().trimmed();
    const QString top_sig = envelope.value(QStringLiteral("signature")).toString().trimmed();
    if (!top_key_id.isEmpty() && !top_sig.isEmpty()) {
        candidates.append(CandidateSig{.keyId = top_key_id, .sigText = top_sig});
    }
    if (candidates.isEmpty()) {
        return std::unexpected(
            UpdateError{QStringLiteral("signature_missing"),
                        QStringLiteral("Manifest envelope contains no Ed25519 signature")});
    }

    bool saw_revoked = false;
    bool saw_unknown_key = false;
    std::optional<TrustedKey> verified_key;
    for (const auto& candidate : candidates) {
        const auto trusted = key_ring.find(candidate.keyId);
        if (!trusted) {
            saw_unknown_key = true;
            continue;
        }
        if (!trusted->isRecovery && revoked_key_ids.contains(trusted->id)) {
            saw_revoked = true;
            continue;
        }
        const auto sig = decodeSignature(candidate.sigText);
        if (!sig) {
            continue;
        }
        if (verifyManifestSignature(raw_payload, *sig, trusted->publicKey)) {
            // Prefer recording a recovery key if multiple valid signatures are present.
            if (!verified_key || (!verified_key->isRecovery && trusted->isRecovery)) {
                verified_key = trusted;
            }
        }
    }
    if (!verified_key) {
        if (saw_revoked) {
            return std::unexpected(
                UpdateError{QStringLiteral("key_revoked"),
                            QStringLiteral("Manifest was signed with a revoked key")});
        }
        if (saw_unknown_key) {
            return std::unexpected(
                UpdateError{QStringLiteral("unknown_key"),
                            QStringLiteral("Manifest was signed with an untrusted key")});
        }
        return std::unexpected(
            UpdateError{QStringLiteral("invalid_signature"),
                        QStringLiteral("Manifest Ed25519 signature verification failed")});
    }

    // Signature is valid over exact raw_payload bytes; now parse and validate payload.
    QJsonParseError payload_error{};
    const QJsonDocument payload_doc = QJsonDocument::fromJson(raw_payload, &payload_error);
    if (payload_error.error != QJsonParseError::NoError || !payload_doc.isObject()) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_json"),
                        QStringLiteral("Signed manifest payload is not valid JSON")});
    }
    const QJsonObject root = payload_doc.object();

    UpdateManifest manifest;
    manifest.verifiedKeyId = verified_key->id;
    manifest.verifiedByRecoveryKey = verified_key->isRecovery;

    manifest.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (manifest.schemaVersion != 1) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_schema"),
                        QStringLiteral("Unsupported signed manifest schemaVersion")});
    }
    manifest.platform = root.value(QStringLiteral("platform")).toString().trimmed();
    if (manifest.platform.isEmpty() ||
        (!expected_platform.isEmpty() && manifest.platform != expected_platform)) {
        return std::unexpected(
            UpdateError{QStringLiteral("platform_mismatch"),
                        QStringLiteral("Manifest platform '%1' does not match expected '%2'")
                            .arg(manifest.platform, expected_platform)});
    }
    manifest.channel = root.value(QStringLiteral("channel")).toString(QStringLiteral("stable"));
    const auto parsed_version = SemVer::parse(root.value(QStringLiteral("version")).toString());
    if (!parsed_version || !parsed_version->isValid()) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_version"),
                        QStringLiteral("Manifest version is not a valid SemVer")});
    }
    manifest.version = *parsed_version;

    const auto seq = parseUnsignedInt64(root.value(QStringLiteral("releaseSeq")));
    if (!seq || *seq == 0) {
        return std::unexpected(
            UpdateError{QStringLiteral("manifest_seq"),
                        QStringLiteral("Manifest releaseSeq must be a positive integer")});
    }
    manifest.releaseSeq = *seq;

    manifest.publishedAt = root.value(QStringLiteral("publishedAt")).toString().trimmed();
    manifest.isMajor = root.value(QStringLiteral("isMajor")).toBool(false);
    manifest.minUpdaterProtocol = root.value(QStringLiteral("minUpdaterProtocol")).toInt(1);
    if (manifest.minUpdaterProtocol < 1) {
        return std::unexpected(UpdateError{QStringLiteral("manifest_protocol"),
                                           QStringLiteral("Invalid minUpdaterProtocol")});
    }
    manifest.requiresFullInstaller =
        root.value(QStringLiteral("requiresFullInstaller")).toBool(false);

    const QString bridge_text = root.value(QStringLiteral("bridgeVersion")).toString().trimmed();
    if (!bridge_text.isEmpty()) {
        const auto bridge = SemVer::parse(bridge_text);
        if (!bridge || !bridge->isValid()) {
            return std::unexpected(UpdateError{QStringLiteral("manifest_bridge"),
                                               QStringLiteral("Invalid bridgeVersion SemVer")});
        }
        manifest.bridgeVersion = *bridge;
    }

    manifest.notes = root.value(QStringLiteral("notes")).toString();
    const QString release_url_text = root.value(QStringLiteral("releaseUrl")).toString().trimmed();
    if (!release_url_text.isEmpty()) {
        const QUrl release_url(release_url_text, QUrl::StrictMode);
        if (!isAllowedUpdateUrl(release_url, url_policy)) {
            return std::unexpected(
                UpdateError{QStringLiteral("manifest_url"),
                            QStringLiteral("Manifest releaseUrl is not an allowed HTTPS URL")});
        }
        manifest.releaseUrl = release_url;
    }

    // Revocations are honored ONLY when the manifest was verified by a recovery key.
    if (manifest.verifiedByRecoveryKey && root.value(QStringLiteral("revokedKeyIds")).isArray()) {
        for (const QJsonValue& item : root.value(QStringLiteral("revokedKeyIds")).toArray()) {
            const QString id = item.toString().trimmed();
            if (!id.isEmpty() && id != verified_key->id) {
                const auto target = key_ring.find(id);
                if (!target || !target->isRecovery) {
                    manifest.revokedKeyIds.append(id);
                }
            }
        }
    }

    auto pack = parseOptionalArtifact(root, QStringLiteral("pack"), url_policy);
    if (!pack) {
        return std::unexpected(pack.error());
    }
    manifest.pack = std::move(*pack);

    auto installer = parseOptionalArtifact(root, QStringLiteral("installer"), url_policy);
    if (!installer) {
        return std::unexpected(installer.error());
    }
    manifest.installer = std::move(*installer);

    auto portable = parseOptionalArtifact(root, QStringLiteral("portable"), url_policy);
    if (!portable) {
        return std::unexpected(portable.error());
    }
    manifest.portable = std::move(*portable);

    auto appimage = parseOptionalArtifact(root, QStringLiteral("appimage"), url_policy);
    if (!appimage) {
        return std::unexpected(appimage.error());
    }
    manifest.appimage = std::move(*appimage);

    auto deb = parseOptionalArtifact(root, QStringLiteral("deb"), url_policy);
    if (!deb) {
        return std::unexpected(deb.error());
    }
    manifest.deb = std::move(*deb);

    if (root.value(QStringLiteral("files")).isArray()) {
        QSet<QString> seen_paths;
        for (const QJsonValue& item : root.value(QStringLiteral("files")).toArray()) {
            if (!item.isObject()) {
                return std::unexpected(
                    UpdateError{QStringLiteral("manifest_files"),
                                QStringLiteral("File entry must be a JSON object")});
            }
            const QJsonObject obj = item.toObject();
            UpdateFileEntry entry;
            entry.path = obj.value(QStringLiteral("path")).toString();
            if (!isSafeRelativeUpdatePath(entry.path)) {
                return std::unexpected(
                    UpdateError{QStringLiteral("unsafe_path"),
                                QStringLiteral("Rejected unsafe file path '%1'").arg(entry.path)});
            }
            entry.kind = obj.value(QStringLiteral("kind")).toString(QStringLiteral("file"));
            entry.encoding = obj.value(QStringLiteral("encoding")).toString(QStringLiteral("zstd"));
            if (entry.kind != QLatin1StringView("file") &&
                entry.kind != QLatin1StringView("solid-tar")) {
                return std::unexpected(UpdateError{
                    QStringLiteral("manifest_files"),
                    QStringLiteral("Unsupported file entry kind '%1'").arg(entry.kind)});
            }
            if (entry.kind == QLatin1StringView("file") &&
                entry.encoding != QLatin1StringView("zstd") &&
                entry.encoding != QLatin1StringView("raw")) {
                return std::unexpected(UpdateError{
                    QStringLiteral("manifest_files"),
                    QStringLiteral("Unsupported file encoding '%1'").arg(entry.encoding)});
            }
            if (entry.kind == QLatin1StringView("solid-tar") &&
                entry.encoding != QLatin1StringView("tar+zstd")) {
                return std::unexpected(
                    UpdateError{QStringLiteral("manifest_files"),
                                QStringLiteral("solid-tar entry must use 'tar+zstd' encoding")});
            }

            const auto size = parseUnsignedInt64(obj.value(QStringLiteral("size")));
            const auto pack_offset = parseUnsignedInt64(obj.value(QStringLiteral("packOffset")));
            const auto pack_size = parseUnsignedInt64(obj.value(QStringLiteral("packSize")));
            if (!size || !pack_offset || !pack_size) {
                return std::unexpected(
                    UpdateError{QStringLiteral("manifest_files"),
                                QStringLiteral("File entry '%1' has invalid size or pack span")
                                    .arg(entry.path)});
            }
            entry.size = static_cast<qint64>(*size);
            entry.packOffset = static_cast<qint64>(*pack_offset);
            entry.packSize = static_cast<qint64>(*pack_size);
            if (manifest.pack &&
                (entry.packOffset + entry.packSize > manifest.pack->size || entry.packSize == 0)) {
                return std::unexpected(UpdateError{
                    QStringLiteral("manifest_files"),
                    QStringLiteral("File entry '%1' exceeds pack bounds").arg(entry.path)});
            }

            entry.sha256 = normalizeSha256Hex(obj.value(QStringLiteral("sha256")).toString());
            if (entry.sha256.isEmpty()) {
                return std::unexpected(UpdateError{
                    QStringLiteral("manifest_files"),
                    QStringLiteral("File entry '%1' has invalid sha256").arg(entry.path)});
            }
            entry.group = obj.value(QStringLiteral("group")).toString();
            entry.executable = obj.value(QStringLiteral("executable")).toBool(false);

            if (entry.kind == QLatin1StringView("file")) {
                const QString key_lower = entry.path.toLower();
                if (seen_paths.contains(key_lower)) {
                    return std::unexpected(UpdateError{
                        QStringLiteral("duplicate_path"),
                        QStringLiteral("Duplicate manifest path '%1'").arg(entry.path)});
                }
                seen_paths.insert(key_lower);
            } else {
                const QJsonValue members_val = obj.value(QStringLiteral("members"));
                if (!members_val.isArray() || members_val.toArray().isEmpty()) {
                    return std::unexpected(UpdateError{
                        QStringLiteral("manifest_files"),
                        QStringLiteral("solid-tar '%1' must declare members").arg(entry.path)});
                }
                for (const QJsonValue& m_val : members_val.toArray()) {
                    const QJsonObject m_obj = m_val.toObject();
                    UpdateSolidMember member;
                    member.path = m_obj.value(QStringLiteral("path")).toString();
                    if (!isSafeRelativeUpdatePath(member.path)) {
                        return std::unexpected(
                            UpdateError{QStringLiteral("unsafe_path"),
                                        QStringLiteral("Rejected unsafe solid-tar member path '%1'")
                                            .arg(member.path)});
                    }
                    const auto m_size = parseUnsignedInt64(m_obj.value(QStringLiteral("size")));
                    if (!m_size) {
                        return std::unexpected(UpdateError{
                            QStringLiteral("manifest_files"),
                            QStringLiteral("Invalid size for member '%1'").arg(member.path)});
                    }
                    member.size = static_cast<qint64>(*m_size);
                    member.sha256 =
                        normalizeSha256Hex(m_obj.value(QStringLiteral("sha256")).toString());
                    if (member.sha256.isEmpty()) {
                        return std::unexpected(UpdateError{
                            QStringLiteral("manifest_files"),
                            QStringLiteral("Invalid sha256 for member '%1'").arg(member.path)});
                    }
                    const QString key_lower = member.path.toLower();
                    if (seen_paths.contains(key_lower)) {
                        return std::unexpected(
                            UpdateError{QStringLiteral("duplicate_path"),
                                        QStringLiteral("Duplicate manifest member path '%1'")
                                            .arg(member.path)});
                    }
                    seen_paths.insert(key_lower);
                    entry.members.append(std::move(member));
                }
            }
            manifest.files.append(std::move(entry));
        }
    }

    return manifest;
}

std::expected<bool, UpdateError> checkUpgradeEligibility(const UpdateManifest& manifest,
                                                         const SemVer& current_version,
                                                         std::uint64_t installed_release_seq) {
    if (manifest.version < current_version) {
        return std::unexpected(
            UpdateError{QStringLiteral("downgrade_version"),
                        QStringLiteral("Rejected downgrade from %1 to %2")
                            .arg(current_version.toString(), manifest.version.toString())});
    }
    if (installed_release_seq > 0 && manifest.releaseSeq < installed_release_seq) {
        return std::unexpected(UpdateError{QStringLiteral("downgrade_seq"),
                                           QStringLiteral("Rejected releaseSeq rollback (%1 < %2)")
                                               .arg(manifest.releaseSeq)
                                               .arg(installed_release_seq)});
    }
    if (manifest.version == current_version) {
        return false; // already up to date
    }
    if (installed_release_seq > 0 && manifest.releaseSeq <= installed_release_seq) {
        return std::unexpected(
            UpdateError{QStringLiteral("downgrade_seq"),
                        QStringLiteral("Version increased without advancing releaseSeq (%1 <= %2)")
                            .arg(manifest.releaseSeq)
                            .arg(installed_release_seq)});
    }
    return true;
}

std::expected<ApiReleaseEnvelope, UpdateError>
parseApiReleaseResponse(const QByteArray& json_bytes, const QString& platform,
                        const UrlPolicy& url_policy) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json_bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(UpdateError{
            QStringLiteral("api_json"), QStringLiteral("Invalid desktop release JSON from API")});
    }
    const QJsonObject root = doc.object();
    ApiReleaseEnvelope out;
    out.paused = root.value(QStringLiteral("paused")).toBool(false);
    out.channel = root.value(QStringLiteral("channel")).toString(QStringLiteral("stable"));
    out.notes = root.value(QStringLiteral("notes")).toString();

    if (const QString ver = root.value(QStringLiteral("version")).toString().trimmed();
        !ver.isEmpty()) {
        out.latestVersion = SemVer::parse(ver);
    }
    if (const auto seq = parseUnsignedInt64(root.value(QStringLiteral("releaseSeq"))); seq) {
        out.releaseSeq = *seq;
    }
    if (const QString min_ver =
            root.value(QStringLiteral("minSupportedVersion")).toString().trimmed();
        !min_ver.isEmpty()) {
        const auto parsed = SemVer::parse(min_ver);
        if (parsed && parsed->isValid()) {
            out.minSupportedVersion = *parsed;
        }
    }
    if (const QString rel_url = root.value(QStringLiteral("releaseUrl")).toString().trimmed();
        !rel_url.isEmpty()) {
        const QUrl candidate(rel_url, QUrl::StrictMode);
        if (isAllowedUpdateUrl(candidate, url_policy)) {
            out.releaseUrl = candidate;
        }
    }

    if (out.paused) {
        return out;
    }

    // Locate platform manifest block under `manifests`.
    const QJsonObject manifests = root.value(QStringLiteral("manifests")).toObject();
    QJsonValue platform_val;
    if (platform == QLatin1StringView("win64")) {
        platform_val = manifests.value(QStringLiteral("win64"));
    } else if (platform == QLatin1StringView("linux-x64")) {
        platform_val = manifests.value(QStringLiteral("linuxX64"));
        if (platform_val.isUndefined() || platform_val.isNull()) {
            platform_val = manifests.value(QStringLiteral("linux-x64"));
        }
    } else {
        platform_val = manifests.value(platform);
    }

    if (platform_val.isObject()) {
        const QJsonObject mobj = platform_val.toObject();
        const QJsonValue env_val = mobj.value(QStringLiteral("envelope"));
        if (env_val.isObject()) {
            out.inlineEnvelopeJson =
                QJsonDocument(env_val.toObject()).toJson(QJsonDocument::Compact);
        } else if (env_val.isString()) {
            out.inlineEnvelopeJson = env_val.toString().toUtf8();
        }
        bool invalid_url = false;
        out.manifestUrls = parseUrlList(mobj, QStringLiteral("urls"), QStringLiteral("url"),
                                        url_policy, invalid_url);
    } else if (platform_val.isString()) {
        const QUrl url(platform_val.toString().trimmed(), QUrl::StrictMode);
        if (isAllowedUpdateUrl(url, url_policy)) {
            out.manifestUrls.append(url);
        }
    }

    // Optional mirror URL overrides/additions from `assets`.
    const QJsonValue assets_val = root.value(QStringLiteral("assets"));
    if (assets_val.isObject()) {
        const QJsonObject assets = assets_val.toObject();
        for (auto it = assets.begin(); it != assets.end(); ++it) {
            QList<QUrl> urls;
            bool invalid_url = false;
            if (it.value().isObject()) {
                urls = parseUrlList(it.value().toObject(), QStringLiteral("urls"),
                                    QStringLiteral("url"), url_policy, invalid_url);
            } else if (it.value().isArray()) {
                for (const QJsonValue& item : it.value().toArray()) {
                    const QUrl url(item.toString().trimmed(), QUrl::StrictMode);
                    if (isAllowedUpdateUrl(url, url_policy)) {
                        urls.append(url);
                    }
                }
            }
            if (!urls.isEmpty()) {
                out.extraAssetUrls.insert(it.key(), urls);
            }
        }
    } else if (assets_val.isArray()) {
        for (const QJsonValue& item : assets_val.toArray()) {
            const QJsonObject obj = item.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
            const QString filename = obj.value(QStringLiteral("filename")).toString().trimmed();
            bool invalid_url = false;
            const QList<QUrl> urls = parseUrlList(obj, QStringLiteral("urls"),
                                                  QStringLiteral("url"), url_policy, invalid_url);
            if (!urls.isEmpty()) {
                if (!filename.isEmpty()) {
                    out.extraAssetUrls[filename].append(urls);
                }
                if (!name.isEmpty()) {
                    out.extraAssetUrls[name].append(urls);
                }
            }
        }
    }

    if (out.inlineEnvelopeJson.isEmpty() && out.manifestUrls.isEmpty()) {
        return std::unexpected(
            UpdateError{QStringLiteral("api_no_manifest"),
                        QStringLiteral("API release response does not provide a manifest for '%1'")
                            .arg(platform)});
    }
    return out;
}

void mergeExtraMirrorUrls(UpdateManifest& manifest,
                          const QHash<QString, QList<QUrl>>& extra_asset_urls,
                          const UrlPolicy& url_policy) {
    auto merge_one = [&](std::optional<UpdateArtifact>& artifact, const QString& logical_key) {
        if (!artifact) {
            return;
        }
        if (extra_asset_urls.contains(artifact->filename)) {
            appendUniqueAllowedUrls(artifact->urls, extra_asset_urls.value(artifact->filename),
                                    url_policy);
        }
        if (extra_asset_urls.contains(logical_key)) {
            appendUniqueAllowedUrls(artifact->urls, extra_asset_urls.value(logical_key),
                                    url_policy);
        }
    };
    merge_one(manifest.pack, QStringLiteral("pack"));
    merge_one(manifest.installer, QStringLiteral("installer"));
    merge_one(manifest.portable, QStringLiteral("portable"));
    merge_one(manifest.appimage, QStringLiteral("appimage"));
    merge_one(manifest.deb, QStringLiteral("deb"));
}

} // namespace animehub::update
