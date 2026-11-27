#ifndef ANIMEHUB_CORE_UPDATE_AHPACK_H
#define ANIMEHUB_CORE_UPDATE_AHPACK_H

#include "update_http.h"
#include "update_types.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <expected>
#include <optional>

class QNetworkAccessManager;

namespace animehub::update {

inline constexpr qint64 kAhpackCoalesceGapBytes = 32 * 1024;

struct InstalledFileRecord {
    qint64 size = 0;
    QByteArray sha256;
    qint64 mtimeMs = 0;
};

struct InstalledManifest {
    int schemaVersion = 1;
    QString version;
    std::uint64_t releaseSeq = 0;
    QHash<QString, InstalledFileRecord> files; // relative POSIX path -> record
};

struct ByteRange {
    qint64 start = 0;
    qint64 end = 0; // inclusive

    [[nodiscard]] qint64 length() const {
        return end >= start ? (end - start + 1) : 0;
    }
};

struct AhpackDiff {
    QList<UpdateFileEntry> entriesToDownload;
    QList<ByteRange> coalescedRanges;
    QStringList filesToDelete;
    qint64 downloadBytes = 0;
    int unchangedFileCount = 0;
};

// Computes SHA-256 of a local file as 64 lowercase hex ASCII bytes, or error.
[[nodiscard]] std::expected<QByteArray, UpdateError> computeFileSha256(const QString& file_path);

// Loads `<install_dir>/installed-manifest.json` if present and valid.
[[nodiscard]] std::optional<InstalledManifest> loadInstalledManifest(const QString& manifest_path);

// Writes `installed-manifest.json` recording the target manifest's files (sampling local mtimes
// from `staging_dir` / `install_dir` when available).
[[nodiscard]] std::expected<void, UpdateError>
writeInstalledManifestForTarget(const QString& output_path, const UpdateManifest& manifest,
                                const QString& install_dir, const QString& staging_dir = {});

// Computes minimal delta between `install_dir` and target `manifest`:
// uses `installed-manifest.json` (validated by size + mtime) as fast path, falling back to SHA-256.
[[nodiscard]] std::expected<AhpackDiff, UpdateError>
computeAhpackDiff(const QString& install_dir, const UpdateManifest& manifest);

// Coalesces entry byte ranges separated by <= `gap_bytes` into minimal single-range fetches.
[[nodiscard]] QList<ByteRange> coalesceByteRanges(const QList<UpdateFileEntry>& entries,
                                                  qint64 gap_bytes = kAhpackCoalesceGapBytes);

// Extracts one `UpdateFileEntry` (either `kind == "file"` with `zstd`/`raw` OR
// `kind == "solid-tar"` with `tar+zstd`) from its compressed pack slice into `staging_dir`,
// verifying SHA-256 of every extracted file against the signed manifest.
[[nodiscard]] std::expected<QList<UpdatePlanFile>, UpdateError>
extractAhpackEntry(const UpdateFileEntry& entry, const QByteArray& packed_bytes,
                   const QString& staging_dir);

// Streams coalesced ranges from `.ahpack` mirrors (supporting HTTP 206, transparent 200 OK
// single-pass streaming fallback, redirect allowlist, and mirror failover), extracting and
// SHA-256 verifying every changed file into `staging_dir`.
class AhpackDownloader final : public QObject {
    Q_OBJECT

public:
    AhpackDownloader(QNetworkAccessManager& network, UrlPolicy policy, QObject* parent = nullptr);
    ~AhpackDownloader() override;

    void start(const UpdateManifest& manifest, const AhpackDiff& diff, const QString& staging_dir);
    void cancel();
    [[nodiscard]] bool isActive() const {
        return active_;
    }

signals:
    void progressChanged(qint64 downloaded_bytes, qint64 total_bytes, qint64 speed_bytes_per_sec);
    void finished(bool ok, const QList<animehub::update::UpdatePlanFile>& staged_files,
                  const QString& error_message);

private:
    void fetchNextRange();
    void handleRangeComplete(const ByteRange& range, const QByteArray& bytes);
    bool handleFullStreamChunk(const QByteArray& chunk);
    void extractEntryFromBuffer(const UpdateFileEntry& entry, const QByteArray& bytes);
    void updateSpeedAndEmit(qint64 current_downloaded);
    void finishSuccess();
    void finishError(const QString& message);

    QNetworkAccessManager& network_;
    UrlPolicy policy_;
    UpdateHttpRequest* request_ = nullptr;

    QList<QUrl> pack_urls_;
    qint64 pack_total_size_ = 0;
    QList<UpdateFileEntry> entries_;
    QList<ByteRange> ranges_;
    QString staging_dir_;

    bool active_ = false;
    bool full_stream_mode_ = false;
    qsizetype range_index_ = 0;
    qint64 completed_range_bytes_ = 0;
    qint64 total_expected_bytes_ = 0;

    // State for 200 OK full-stream fallback mode.
    qint64 stream_pos_ = 0;
    qsizetype stream_entry_index_ = 0;
    QByteArray stream_entry_buf_;
    QString stream_error_;

    QList<UpdatePlanFile> staged_files_;
    QElapsedTimer elapsed_;
    qint64 last_speed_ms_ = 0;
    qint64 last_speed_bytes_ = 0;
    qint64 current_speed_bps_ = 0;
};

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_AHPACK_H
