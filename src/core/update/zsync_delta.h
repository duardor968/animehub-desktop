#ifndef ANIMEHUB_CORE_UPDATE_ZSYNC_DELTA_H
#define ANIMEHUB_CORE_UPDATE_ZSYNC_DELTA_H

#include "update_http.h"
#include "update_types.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <expected>
#include <vector>

class QNetworkAccessManager;

namespace animehub::update {

struct ZsyncControlFile {
    QString filename;
    qint64 length = 0;
    int blockSize = 0;
    int seqMatches = 1;
    int rsumBytes = 2;
    int checksumBytes = 5;
    QString sha1Hex;
    qint64 blockCount = 0;
    // Raw packed block-sum records: `blockCount * (rsumBytes + checksumBytes)` bytes.
    QByteArray blockSums;
};

struct ZsyncMatchPlan {
    // For each target block [0 .. blockCount-1]: offset in seed file, or -1 if missing.
    std::vector<qint64> seedBlockOffsets;
    qint64 matchedBlocks = 0;
    qint64 missingBlocks = 0;
    QList<HttpRangeRequest> missingRanges;
    qint64 downloadBytes = 0;
};

// Computes zsync's 32-bit rolling checksum `(a << 16) | b` over a block of bytes.
[[nodiscard]] std::uint32_t computeZsyncRsum(const std::uint8_t* data, std::size_t len);

// Computes truncated MD4 strong checksum (`checksum_bytes` <= 16) of a block, zero-padded to
// `block_size` if `len < block_size` (zsync convention for the final block).
[[nodiscard]] QByteArray computeZsyncStrongHash(const std::uint8_t* data, std::size_t len,
                                                int block_size, int checksum_bytes);

// Parses a `.zsync` control file header and binary block-hash table.
[[nodiscard]] std::expected<ZsyncControlFile, UpdateError>
parseZsyncControlFile(const QByteArray& bytes);

// Scans `seed_file_path` with rolling checksum + MD4 verification to identify reusable blocks
// and returns coalesced single-range HTTP byte spans for the missing blocks.
[[nodiscard]] std::expected<ZsyncMatchPlan, UpdateError>
planZsyncDelta(const ZsyncControlFile& zsync, const QString& seed_file_path,
               qint64 coalesce_gap_bytes = 32 * 1024);

// Downloads a Linux AppImage via single-range `.zsync` delta (falling back to full AppImage
// download if `.zsync` is unavailable, fails, or >80% of blocks changed), and ALWAYS verifies
// the final reconstructed file's full SHA-256 against the Ed25519-signed manifest.
class ZsyncDownloader final : public QObject {
    Q_OBJECT

public:
    ZsyncDownloader(QNetworkAccessManager& network, UrlPolicy policy, QObject* parent = nullptr);
    ~ZsyncDownloader() override;

    void start(const UpdateArtifact& appimage_artifact, const QString& seed_appimage_path,
               const QString& output_path);
    void cancel();
    [[nodiscard]] bool isActive() const {
        return active_;
    }

signals:
    void progressChanged(qint64 downloaded_bytes, qint64 total_bytes, qint64 speed_bytes_per_sec);
    void finished(bool ok, const QString& output_path, bool used_delta,
                  const QString& error_message);

private:
    void startFullDownload();
    void startDeltaAssembly();
    void fetchNextMissingRange();
    void finalizeAndVerify(bool used_delta);
    void updateSpeedAndEmit(qint64 current_downloaded);
    void finishError(const QString& message);

    QNetworkAccessManager& network_;
    UrlPolicy policy_;
    UpdateHttpRequest* request_ = nullptr;

    UpdateArtifact artifact_;
    QString seed_path_;
    QString output_path_;
    QString temp_path_;

    bool active_ = false;
    bool using_delta_ = false;
    ZsyncControlFile zsync_;
    ZsyncMatchPlan plan_;
    qsizetype range_index_ = 0;
    qint64 completed_bytes_ = 0;
    qint64 total_expected_bytes_ = 0;

    QElapsedTimer elapsed_;
    qint64 last_speed_ms_ = 0;
    qint64 last_speed_bytes_ = 0;
    qint64 current_speed_bps_ = 0;
};

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_ZSYNC_DELTA_H
