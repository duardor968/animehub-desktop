#ifndef ANIMEHUB_CORE_UPDATE_KEYS_H
#define ANIMEHUB_CORE_UPDATE_KEYS_H

#include <QList>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace animehub::update {

using Ed25519PublicKey = std::array<std::uint8_t, 32>;
using Ed25519Signature = std::array<std::uint8_t, 64>;

inline constexpr std::string_view kManifestDomainPrefix = "animehub-update-manifest-v1\n";
inline constexpr int kUpdaterProtocolVersion = 1;

// Production Ed25519 public keys.
// release-2026:  58c3bc5d2293e774c9ed1298adfed66b772a8481414f934bd85a367f309fcc67
// recovery-2026: cdd07bd148fc7b5400bc729f1dd8e87827a76fb3c4e24cd53b502353e10d4825
inline constexpr Ed25519PublicKey kRelease2026PublicKey = {
    0x58, 0xc3, 0xbc, 0x5d, 0x22, 0x93, 0xe7, 0x74, 0xc9, 0xed, 0x12, 0x98, 0xad, 0xfe, 0xd6, 0x6b,
    0x77, 0x2a, 0x84, 0x81, 0x41, 0x4f, 0x93, 0x4b, 0xd8, 0x5a, 0x36, 0x7f, 0x30, 0x9f, 0xcc, 0x67,
};

inline constexpr Ed25519PublicKey kRecovery2026PublicKey = {
    0xcd, 0xd0, 0x7b, 0xd1, 0x48, 0xfc, 0x7b, 0x54, 0x00, 0xbc, 0x72, 0x9f, 0x1d, 0xd8, 0xe8, 0x78,
    0x27, 0xa7, 0x6f, 0xb3, 0xc4, 0xe2, 0x4c, 0xd5, 0x3b, 0x50, 0x23, 0x53, 0xe1, 0x0d, 0x48, 0x25,
};

struct TrustedKey {
    QString id;
    Ed25519PublicKey publicKey{};
    bool isRecovery = false;
};

struct KeyRing {
    QList<TrustedKey> keys;

    [[nodiscard]] static KeyRing production() {
        return KeyRing{.keys = {
                           TrustedKey{.id = QStringLiteral("release-2026"),
                                      .publicKey = kRelease2026PublicKey,
                                      .isRecovery = false},
                           TrustedKey{.id = QStringLiteral("recovery-2026"),
                                      .publicKey = kRecovery2026PublicKey,
                                      .isRecovery = true},
                       }};
    }

    [[nodiscard]] std::optional<TrustedKey> find(const QString& id) const {
        for (const auto& key : keys) {
            if (key.id == id) {
                return key;
            }
        }
        return std::nullopt;
    }
};

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_KEYS_H
