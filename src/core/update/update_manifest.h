#ifndef ANIMEHUB_CORE_UPDATE_MANIFEST_H
#define ANIMEHUB_CORE_UPDATE_MANIFEST_H

#include "update_keys.h"
#include "update_types.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QUrl>

#include <expected>

namespace animehub::update {

struct ApiReleaseEnvelope {
    bool paused = false;
    QString channel;
    std::optional<SemVer> latestVersion;
    std::uint64_t releaseSeq = 0;
    std::optional<SemVer> minSupportedVersion;
    QString notes;
    QUrl releaseUrl;
    // Either inline signed envelope JSON bytes OR manifest URLs to fetch and verify.
    QByteArray inlineEnvelopeJson;
    QList<QUrl> manifestUrls;
    // Optional extra mirror URLs keyed by filename or logical asset name ("pack", "appimage", ...).
    QHash<QString, QList<QUrl>> extraAssetUrls;
};

// Strict validation for every relative path inside manifests, tar archives, and installed records.
[[nodiscard]] bool isSafeRelativeUpdatePath(const QString& path);

// Enforces HTTPS-only (or loopback HTTP when enabled for tests) and host allowlist.
[[nodiscard]] bool isAllowedUpdateUrl(const QUrl& url, const UrlPolicy& policy);

// Parses lowercase/uppercase 64-char SHA-256 hex into normalized lowercase 64-byte ASCII, or empty.
[[nodiscard]] QByteArray normalizeSha256Hex(const QString& text);

// Verifies raw Ed25519 signature over `kManifestDomainPrefix + raw_payload`.
[[nodiscard]] bool verifyManifestSignature(const QByteArray& raw_payload,
                                           const Ed25519Signature& signature,
                                           const Ed25519PublicKey& public_key);

// Parses and cryptographically verifies a signed manifest envelope JSON (`update-<platform>.json`).
[[nodiscard]] std::expected<UpdateManifest, UpdateError>
verifyAndParseManifestEnvelope(const QByteArray& envelope_json, const KeyRing& key_ring,
                               const QSet<QString>& revoked_key_ids,
                               const QString& expected_platform, const UrlPolicy& url_policy);

// Validates anti-downgrade rules: version > current AND (installed_seq == 0 || seq >
// installed_seq). Returns std::nullopt when manifest is up-to-date (equal version and
// equal/unspecified seq), true when manifest is a strictly newer valid upgrade, or UpdateError on
// downgrade/replay.
[[nodiscard]] std::expected<bool, UpdateError>
checkUpgradeEligibility(const UpdateManifest& manifest, const SemVer& current_version,
                        std::uint64_t installed_release_seq);

// Parses `GET /api/v1/desktop/releases/latest` wrapper response from animehub-api.
[[nodiscard]] std::expected<ApiReleaseEnvelope, UpdateError>
parseApiReleaseResponse(const QByteArray& json_bytes, const QString& platform,
                        const UrlPolicy& url_policy);

// Merges verified allowlisted mirror URLs from the API wrapper into the signed manifest's
// artifacts.
void mergeExtraMirrorUrls(UpdateManifest& manifest,
                          const QHash<QString, QList<QUrl>>& extra_asset_urls,
                          const UrlPolicy& url_policy);

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_MANIFEST_H
