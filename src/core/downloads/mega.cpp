#include "mega.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>

#include <vector>

namespace animehub::downloads::mega {
namespace {

constexpr int kMaxAttempts = 4;
constexpr qint64 kMaxApiBody = 64 * 1024;

std::uint32_t readWord(const QByteArray& bytes, qsizetype offset) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 3]));
}

std::uint32_t readWord(const platform::AesBlock& block, std::size_t offset) {
    return (static_cast<std::uint32_t>(block[offset]) << 24) |
           (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
           static_cast<std::uint32_t>(block[offset + 3]);
}

void writeWord(std::uint8_t* out, std::uint32_t word) {
    out[0] = static_cast<std::uint8_t>(word >> 24);
    out[1] = static_cast<std::uint8_t>(word >> 16);
    out[2] = static_cast<std::uint8_t>(word >> 8);
    out[3] = static_cast<std::uint8_t>(word);
}

std::optional<QByteArray> fromBase64Url(QByteArrayView text) {
    auto decoded = QByteArray::fromBase64Encoding(text.toByteArray(),
                                                  QByteArray::Base64UrlEncoding |
                                                      QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        return std::nullopt;
    }
    return std::move(*decoded);
}

bool isHandle(QStringView handle) {
    return handle.size() == 8 && std::ranges::all_of(handle, [](QChar ch) {
               return (ch.isLetterOrNumber() && ch.unicode() < 128) || ch == u'-' || ch == u'_';
           });
}

platform::AesBlock chunkIv(const std::array<std::uint8_t, 8>& nonce) {
    platform::AesBlock iv{};
    std::ranges::copy(nonce, iv.begin());
    std::ranges::copy(nonce, iv.begin() + 8);
    return iv;
}

ApiFailure failureFor(int code) {
    switch (code) {
    case -9:
    case -11:
        return {.kind = ApiFailure::Kind::NotFound, .code = code};
    case -16:
        return {.kind = ApiFailure::Kind::Blocked, .code = code};
    case -17:
        return {.kind = ApiFailure::Kind::OverQuota, .code = code};
    case -3:
    case -18:
        return {.kind = ApiFailure::Kind::Unavailable, .code = code};
    default:
        return {.kind = ApiFailure::Kind::Protocol, .code = code};
    }
}

} // namespace

std::optional<FileLink> parseFileLink(const QUrl& url) {
    const auto host = url.host().toLower();
    if (url.scheme() != u"https" || (host != u"mega.nz" && host != u"www.mega.nz" &&
                                     host != u"mega.co.nz" && host != u"www.mega.co.nz")) {
        return std::nullopt;
    }
    QString handle;
    QString key;
    const auto fragment = url.fragment(QUrl::FullyDecoded);
    if (url.path().startsWith(u"/file/")) {
        handle = url.path().mid(6);
        key = fragment.section(u'/', 0, 0); // "<key>/file/<sub>" points inside a folder
    } else if (fragment.startsWith(u'!')) {
        const auto parts = fragment.split(u'!', Qt::SkipEmptyParts);
        if (parts.size() == 2) {
            handle = parts[0];
            key = parts[1];
        }
    }
    if (!isHandle(handle)) {
        return std::nullopt;
    }
    const auto raw = fromBase64Url(key.toLatin1());
    if (!raw || raw->size() != 32) {
        return std::nullopt;
    }
    FileLink link;
    link.handle = handle;
    std::array<std::uint32_t, 8> words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = readWord(*raw, static_cast<qsizetype>(i * 4));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        writeWord(link.key.data() + i * 4, words[i] ^ words[i + 4]);
    }
    writeWord(link.nonce.data(), words[4]);
    writeWord(link.nonce.data() + 4, words[5]);
    link.metaMac = {words[6], words[7]};
    return link;
}

QString decryptFileName(const QByteArray& attributes_base64url, const platform::AesBlock& key) {
    auto raw = fromBase64Url(attributes_base64url);
    if (!raw || raw->isEmpty() || raw->size() % 16 != 0 || raw->size() > 64 * 1024) {
        return {};
    }
    const platform::Aes128 aes(key);
    platform::AesBlock iv{};
    std::span<std::uint8_t> data(reinterpret_cast<std::uint8_t*>(raw->data()),
                                 static_cast<std::size_t>(raw->size()));
    if (!aes.cbcDecrypt(data, iv) || !raw->startsWith("MEGA{")) {
        return {};
    }
    auto json = raw->mid(4);
    while (json.endsWith('\0')) {
        json.chop(1);
    }
    const auto document = QJsonDocument::fromJson(json);
    return document.object().value(QLatin1StringView("n")).toString();
}

qint64 chunkLength(int index) {
    return static_cast<qint64>(std::min(index + 1, 8)) * 128 * 1024;
}

bool applyKeystream(std::span<std::uint8_t> data, qint64 offset, const platform::Aes128& aes,
                    const std::array<std::uint8_t, 8>& nonce) {
    if (offset % 16 != 0) {
        return false;
    }
    const std::size_t blocks = (data.size() + 15) / 16;
    std::vector<std::uint8_t> stream(blocks * 16);
    auto counter = static_cast<std::uint64_t>(offset / 16);
    for (std::size_t block = 0; block < blocks; ++block, ++counter) {
        auto* out = stream.data() + block * 16;
        std::ranges::copy(nonce, out);
        for (int byte = 0; byte < 8; ++byte) {
            out[8 + byte] = static_cast<std::uint8_t>(counter >> (56 - 8 * byte));
        }
    }
    if (!aes.encryptBlocks(stream)) {
        return false;
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] ^= stream[i];
    }
    return true;
}

std::expected<void, CryptoError> decryptFile(const QString& source, const QString& destination,
                                             const FileLink& link, const std::atomic<bool>& cancel,
                                             const std::function<void(qint64)>& progress) {
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        return std::unexpected(CryptoError::Io);
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        return std::unexpected(CryptoError::Io);
    }
    const platform::Aes128 aes(link.key);
    if (!aes.valid()) {
        output.cancelWriting();
        return std::unexpected(CryptoError::Platform);
    }
    const auto fail = [&output](CryptoError error) {
        output.cancelWriting();
        return std::unexpected(error);
    };
    const qint64 size = input.size();
    std::vector<std::uint8_t> buffer;
    std::vector<std::uint8_t> mac_input;
    std::vector<std::uint8_t> chunk_macs;
    for (qint64 start = 0, index = 0; start < size; ++index) {
        if (cancel.load(std::memory_order_relaxed)) {
            return fail(CryptoError::Cancelled);
        }
        const qint64 length = std::min(chunkLength(static_cast<int>(index)), size - start);
        buffer.resize(static_cast<std::size_t>(length));
        if (input.read(reinterpret_cast<char*>(buffer.data()), length) != length) {
            return fail(CryptoError::Io);
        }
        if (!applyKeystream(buffer, start, aes, link.nonce)) {
            return fail(CryptoError::Platform);
        }
        mac_input.assign(buffer.begin(), buffer.end());
        mac_input.resize((mac_input.size() + 15) / 16 * 16, 0);
        auto iv = chunkIv(link.nonce);
        if (!aes.cbcEncrypt(mac_input, iv)) {
            return fail(CryptoError::Platform);
        }
        chunk_macs.insert(chunk_macs.end(), iv.begin(), iv.end());
        if (output.write(reinterpret_cast<const char*>(buffer.data()), length) != length) {
            return fail(CryptoError::Io);
        }
        start += length;
        if (progress) {
            progress(start);
        }
    }
    platform::AesBlock file_mac{};
    if (!chunk_macs.empty() && !aes.cbcEncrypt(chunk_macs, file_mac)) {
        return fail(CryptoError::Platform);
    }
    const auto first = readWord(file_mac, 0) ^ readWord(file_mac, 4);
    const auto second = readWord(file_mac, 8) ^ readWord(file_mac, 12);
    if (first != link.metaMac[0] || second != link.metaMac[1]) {
        return fail(CryptoError::Integrity);
    }
    if (!output.commit()) {
        return std::unexpected(CryptoError::Io);
    }
    return {};
}

Api::Api(QNetworkAccessManager& network, QObject* parent, QUrl endpoint)
    : QObject(parent), network_(network), endpoint_(std::move(endpoint)),
      sequence_(QRandomGenerator::global()->bounded(1'000'000'000)) {}

void Api::file(const FileLink& link, QObject* context, Callback done) {
    request(link, context, std::move(done), 1);
}

void Api::request(const FileLink& link, QObject* context, Callback done, int attempt) {
    QUrl url = endpoint_;
    url.setQuery(QUrlQuery{{QStringLiteral("id"), QString::number(++sequence_)}});
    QNetworkRequest http(url);
    http.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    http.setTransferTimeout(30'000);
    const QJsonArray body{QJsonObject{{QStringLiteral("a"), QStringLiteral("g")},
                                      {QStringLiteral("g"), 1},
                                      {QStringLiteral("ssl"), 2},
                                      {QStringLiteral("p"), link.handle}}};
    auto* reply = network_.post(http, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(
        reply, &QNetworkReply::finished, context,
        [this, reply, link, context, done = std::move(done), attempt]() mutable {
            const auto retry = [&](ApiFailure failure) {
                if (attempt >= kMaxAttempts) {
                    done(std::unexpected(std::move(failure)));
                    return;
                }
                QTimer::singleShot(1000 * (1 << (attempt - 1)), context,
                                   [this, link, context, done = std::move(done), attempt] {
                                       this->request(link, context, done, attempt + 1);
                                   });
            };
            if (reply->error() != QNetworkReply::NoError) {
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                retry({.kind = status >= 500 ? ApiFailure::Kind::Unavailable
                                             : ApiFailure::Kind::Network,
                       .message = reply->errorString()});
                return;
            }
            const auto raw = reply->read(kMaxApiBody);
            const auto document = QJsonDocument::fromJson(raw);
            QJsonValue answer;
            if (document.isArray() && !document.array().isEmpty()) {
                answer = document.array().first();
            } else if (const auto number = raw.trimmed().toInt();
                       raw.trimmed() == QByteArray::number(number)) {
                answer = number;
            }
            if (answer.isDouble()) {
                auto failure = failureFor(answer.toInt());
                if (failure.kind == ApiFailure::Kind::Unavailable) {
                    retry(std::move(failure));
                } else {
                    done(std::unexpected(std::move(failure)));
                }
                return;
            }
            const auto object = answer.toObject();
            const QUrl download(object.value(QLatin1StringView("g")).toString(), QUrl::StrictMode);
            const auto size = object.value(QLatin1StringView("s")).toInteger(-1);
            // The bytes are encrypted and MAC-checked, so plain HTTP mirrors are acceptable.
            if ((download.scheme() != u"https" && download.scheme() != u"http") || size < 0) {
                done(std::unexpected(ApiFailure{.kind = ApiFailure::Kind::Protocol,
                                                .message = QStringLiteral("unexpected answer")}));
                return;
            }
            done(RemoteFile{
                .url = download,
                .size = size,
                .name = decryptFileName(object.value(QLatin1StringView("at")).toString().toLatin1(),
                                        link.key),
            });
        });
}

} // namespace animehub::downloads::mega
