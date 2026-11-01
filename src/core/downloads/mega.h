#ifndef ANIMEHUB_CORE_DOWNLOADS_MEGA_H
#define ANIMEHUB_CORE_DOWNLOADS_MEGA_H

#include "platform/aes.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

class QNetworkAccessManager;

// Native download of public MEGA file links: the public API gives a temporary URL to the
// encrypted bytes; after the transfer the file is decrypted (AES-128-CTR) into its final name and
// its integrity checked against the MAC carried by the link key.
namespace animehub::downloads::mega {

struct FileLink {
    QString handle;
    platform::AesBlock key{};
    std::array<std::uint8_t, 8> nonce{};
    std::array<std::uint32_t, 2> metaMac{};
};

// `https://mega.nz/file/<handle>#<key>` and the legacy `https://mega.nz/#!<handle>!<key>`.
[[nodiscard]] std::optional<FileLink> parseFileLink(const QUrl& url);

// `https://transfer.it/t/<id>`: a transfer served by MEGA's API whose file keys come in the clear.
// Returns its 12-character id.
[[nodiscard]] std::optional<QString> parseTransferLink(const QUrl& url);

// The file name stored in the encrypted attributes, or empty when they do not decrypt.
[[nodiscard]] QString decryptFileName(const QByteArray& attributes_base64url,
                                      const platform::AesBlock& key);

// MAC chunk layout: 128 KiB, 256 KiB, ... 1 MiB, then 1 MiB each.
[[nodiscard]] qint64 chunkLength(int index);

enum class CryptoError { Io, Cancelled, Integrity, Platform };

// Decrypts `source` into `destination` and verifies the MAC. `destination` only appears, whole,
// when the MAC matches; `source` is left untouched so an interrupted run can start over.
// `progress(bytes)` runs on the calling thread.
std::expected<void, CryptoError> decryptFile(const QString& source, const QString& destination,
                                             const FileLink& link, const std::atomic<bool>& cancel,
                                             const std::function<void(qint64)>& progress);

// Encrypts in place (CTR is symmetric); used by tests to build MEGA-shaped files.
bool applyKeystream(std::span<std::uint8_t> data, qint64 offset, const platform::Aes128& aes,
                    const std::array<std::uint8_t, 8>& nonce);

struct RemoteFile {
    QUrl url; // temporary, never persisted
    qint64 size = 0;
    QString name;
};

struct ApiFailure {
    enum class Kind { NotFound, Blocked, OverQuota, Unavailable, Network, Protocol };
    Kind kind = Kind::Protocol;
    int code = 0;
    QString message;
    // OverQuota: when the transfer quota frees, if MEGA said so (`tl` in the answer).
    std::optional<std::chrono::seconds> timeLeft;
};

// Seconds until the transfer quota frees, as MEGA's storage servers state it on a 509 answer
// (`X-MEGA-Time-Left`; read the same way by MEGA's own SDK). None when absent or not positive.
[[nodiscard]] std::optional<std::chrono::seconds> quotaTimeLeft(QByteArrayView header);

// The one video of a transfer.it transfer: where to download it and the key to decrypt it.
struct TransferFile {
    RemoteFile file;
    FileLink link;
};

// Client for the `g` command of MEGA's public API.
class Api final : public QObject {
    Q_OBJECT

public:
    explicit Api(QNetworkAccessManager& network, QObject* parent = nullptr,
                 QUrl endpoint = QUrl(QStringLiteral("https://g.api.mega.co.nz/cs")));

    using Callback = std::function<void(const std::expected<RemoteFile, ApiFailure>&)>;
    void file(const FileLink& link, QObject* context, Callback done);
    // Lists a transfer.it transfer and asks the temporary URL of its single video; several videos
    // answer as a Protocol failure.
    using TransferCallback = std::function<void(const std::expected<TransferFile, ApiFailure>&)>;
    void transfer(const QString& transfer_id, QObject* context, TransferCallback done);
    // Asks a temporary download URL for one byte and reads how long its 509 says the quota
    // lasts: aria2 reports the status but not the headers.
    void quotaTimeLeft(const QUrl& url, QObject* context,
                       std::function<void(std::optional<std::chrono::seconds>)> done);

private:
    using CommandCallback = std::function<void(const std::expected<QJsonObject, ApiFailure>&)>;
    void command(QUrlQuery query, QJsonObject body, QObject* context, CommandCallback done,
                 int attempt);

    QNetworkAccessManager& network_;
    QUrl endpoint_;
    quint64 sequence_;
};

} // namespace animehub::downloads::mega

#endif // ANIMEHUB_CORE_DOWNLOADS_MEGA_H
