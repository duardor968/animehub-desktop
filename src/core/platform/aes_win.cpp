#include "aes.h"

#include <qt_windows.h>

#include <bcrypt.h>

namespace animehub::platform {
namespace {

bool succeeded(NTSTATUS status) {
    return status >= 0;
}

struct Handles {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;

    bool open(const wchar_t* mode, const AesBlock& secret) {
        if (!succeeded(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
            algorithm = nullptr;
            return false;
        }
        const auto mode_bytes = static_cast<ULONG>((wcslen(mode) + 1) * sizeof(wchar_t));
        if (!succeeded(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                                         reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
                                         mode_bytes, 0))) {
            return false;
        }
        return succeeded(BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0,
                                                    const_cast<PUCHAR>(secret.data()),
                                                    static_cast<ULONG>(secret.size()), 0));
    }

    void close() {
        if (key != nullptr) {
            BCryptDestroyKey(key);
            key = nullptr;
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
        }
    }
};

} // namespace

struct Aes128::State {
    Handles ecb;
    Handles cbc;
    bool valid = false;

    ~State() {
        ecb.close();
        cbc.close();
    }
};

Aes128::Aes128(const AesBlock& key) : state_(std::make_unique<State>()) {
    state_->valid = state_->ecb.open(BCRYPT_CHAIN_MODE_ECB, key) &&
                    state_->cbc.open(BCRYPT_CHAIN_MODE_CBC, key);
}

Aes128::Aes128(Aes128&&) noexcept = default;
Aes128& Aes128::operator=(Aes128&&) noexcept = default;
Aes128::~Aes128() = default;

bool Aes128::valid() const {
    return state_ && state_->valid;
}

bool Aes128::encryptBlocks(std::span<std::uint8_t> data) const {
    if (!valid() || data.size() % 16 != 0) {
        return false;
    }
    ULONG written = 0;
    return succeeded(BCryptEncrypt(state_->ecb.key, data.data(), static_cast<ULONG>(data.size()),
                                   nullptr, nullptr, 0, data.data(),
                                   static_cast<ULONG>(data.size()), &written, 0)) &&
           written == data.size();
}

bool Aes128::decryptBlocks(std::span<std::uint8_t> data) const {
    if (!valid() || data.size() % 16 != 0) {
        return false;
    }
    ULONG written = 0;
    return succeeded(BCryptDecrypt(state_->ecb.key, data.data(), static_cast<ULONG>(data.size()),
                                   nullptr, nullptr, 0, data.data(),
                                   static_cast<ULONG>(data.size()), &written, 0)) &&
           written == data.size();
}

bool Aes128::cbcEncrypt(std::span<std::uint8_t> data, AesBlock& iv) const {
    if (!valid() || data.size() % 16 != 0) {
        return false;
    }
    ULONG written = 0;
    // CNG updates `iv` with the last ciphertext block, which is what chaining needs.
    return succeeded(BCryptEncrypt(state_->cbc.key, data.data(), static_cast<ULONG>(data.size()),
                                   nullptr, iv.data(), static_cast<ULONG>(iv.size()), data.data(),
                                   static_cast<ULONG>(data.size()), &written, 0)) &&
           written == data.size();
}

bool Aes128::cbcDecrypt(std::span<std::uint8_t> data, AesBlock& iv) const {
    if (!valid() || data.size() % 16 != 0) {
        return false;
    }
    ULONG written = 0;
    return succeeded(BCryptDecrypt(state_->cbc.key, data.data(), static_cast<ULONG>(data.size()),
                                   nullptr, iv.data(), static_cast<ULONG>(iv.size()), data.data(),
                                   static_cast<ULONG>(data.size()), &written, 0)) &&
           written == data.size();
}

} // namespace animehub::platform
