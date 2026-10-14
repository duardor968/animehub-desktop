#ifndef ANIMEHUB_CORE_PLATFORM_AES_H
#define ANIMEHUB_CORE_PLATFORM_AES_H

#include <QByteArray>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace animehub::platform {

using AesBlock = std::array<std::uint8_t, 16>;

// AES-128 backed by the operating system (Windows CNG). Only what the MEGA and MyJDownloader
// protocols need: raw block encryption, and CBC with an explicit IV.
class Aes128 {
public:
    explicit Aes128(const AesBlock& key);
    Aes128(const Aes128&) = delete;
    Aes128& operator=(const Aes128&) = delete;
    Aes128(Aes128&&) noexcept;
    Aes128& operator=(Aes128&&) noexcept;
    ~Aes128();

    [[nodiscard]] bool valid() const;

    // ECB encryption in place; `data.size()` must be a multiple of 16.
    bool encryptBlocks(std::span<std::uint8_t> data) const;
    bool decryptBlocks(std::span<std::uint8_t> data) const;
    // CBC in place, without padding; `iv` is updated to the chaining value after the call so
    // a long stream can be processed in pieces.
    bool cbcEncrypt(std::span<std::uint8_t> data, AesBlock& iv) const;
    bool cbcDecrypt(std::span<std::uint8_t> data, AesBlock& iv) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_AES_H
