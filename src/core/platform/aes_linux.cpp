#include "aes.h"

#include <openssl/evp.h>

#include <limits>

namespace animehub::platform {
namespace {

struct ContextFree {
    void operator()(EVP_CIPHER_CTX* context) const {
        EVP_CIPHER_CTX_free(context);
    }
};
using Context = std::unique_ptr<EVP_CIPHER_CTX, ContextFree>;

// One direction of one mode, without padding: the protocols only process whole blocks.
Context open(const EVP_CIPHER* cipher, const AesBlock& key, bool encrypt) {
    Context context(EVP_CIPHER_CTX_new());
    if (!context ||
        EVP_CipherInit_ex(context.get(), cipher, nullptr, key.data(), nullptr, encrypt ? 1 : 0) !=
            1 ||
        EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1) {
        return nullptr;
    }
    return context;
}

// In place; OpenSSL allows the input and the output to be the same buffer.
bool update(EVP_CIPHER_CTX* context, std::span<std::uint8_t> data) {
    if (data.size() % 16 != 0 || data.size() > std::numeric_limits<int>::max()) {
        return false;
    }
    int written = 0;
    return EVP_CipherUpdate(context, data.data(), &written, data.data(),
                            static_cast<int>(data.size())) == 1 &&
           static_cast<std::size_t>(written) == data.size();
}

// CBC starts from `iv` and leaves in it the chaining value for the next piece.
bool chain(EVP_CIPHER_CTX* context, std::span<std::uint8_t> data, AesBlock& iv) {
    return EVP_CipherInit_ex(context, nullptr, nullptr, nullptr, iv.data(), -1) == 1 &&
           update(context, data) &&
           EVP_CIPHER_CTX_get_updated_iv(context, iv.data(), iv.size()) == 1;
}

} // namespace

struct Aes128::State {
    Context ecbEncrypt;
    Context ecbDecrypt;
    Context cbcEncrypt;
    Context cbcDecrypt;
};

Aes128::Aes128(const AesBlock& key) : state_(std::make_unique<State>()) {
    state_->ecbEncrypt = open(EVP_aes_128_ecb(), key, true);
    state_->ecbDecrypt = open(EVP_aes_128_ecb(), key, false);
    state_->cbcEncrypt = open(EVP_aes_128_cbc(), key, true);
    state_->cbcDecrypt = open(EVP_aes_128_cbc(), key, false);
}

Aes128::Aes128(Aes128&&) noexcept = default;
Aes128& Aes128::operator=(Aes128&&) noexcept = default;
Aes128::~Aes128() = default;

bool Aes128::valid() const {
    return state_ && state_->ecbEncrypt && state_->ecbDecrypt && state_->cbcEncrypt &&
           state_->cbcDecrypt;
}

bool Aes128::encryptBlocks(std::span<std::uint8_t> data) const {
    return valid() && update(state_->ecbEncrypt.get(), data);
}

bool Aes128::decryptBlocks(std::span<std::uint8_t> data) const {
    return valid() && update(state_->ecbDecrypt.get(), data);
}

bool Aes128::cbcEncrypt(std::span<std::uint8_t> data, AesBlock& iv) const {
    return valid() && chain(state_->cbcEncrypt.get(), data, iv);
}

bool Aes128::cbcDecrypt(std::span<std::uint8_t> data, AesBlock& iv) const {
    return valid() && chain(state_->cbcDecrypt.get(), data, iv);
}

} // namespace animehub::platform
