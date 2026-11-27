#include "zsync_delta.h"

#include "ahpack.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cstring>
#include <vector>

namespace animehub::update {
namespace {

constexpr double kZsyncFallbackRatio = 0.80;

std::uint32_t rsumMaskForBytes(int rsum_bytes) {
    if (rsum_bytes >= 4) {
        return 0xFFFFFFFFU;
    }
    return (1U << (static_cast<unsigned>(rsum_bytes) * 8U)) - 1U;
}

std::uint32_t readRsumFromTable(const char* ptr, int rsum_bytes) {
    std::uint32_t value = 0;
    for (int i = 0; i < rsum_bytes; ++i) {
        value = (value << 8U) | static_cast<unsigned char>(ptr[i]);
    }
    return value;
}

bool isPowerOfTwo(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

} // namespace

std::uint32_t computeZsyncRsum(const std::uint8_t* data, std::size_t len) {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::size_t rem = len;
    for (std::size_t i = 0; i < len; ++i) {
        const std::uint16_t c = data[i];
        a = static_cast<std::uint16_t>(a + c);
        b = static_cast<std::uint16_t>(b + static_cast<std::uint16_t>(rem * c));
        --rem;
    }
    return (static_cast<std::uint32_t>(a) << 16U) | static_cast<std::uint32_t>(b);
}

QByteArray computeZsyncStrongHash(const std::uint8_t* data, std::size_t len, int block_size,
                                  int checksum_bytes) {
    QCryptographicHash md4(QCryptographicHash::Md4);
    if (len > 0) {
        md4.addData(
            QByteArrayView(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len)));
    }
    if (static_cast<int>(len) < block_size) {
        const QByteArray padding(block_size - static_cast<int>(len), '\0');
        md4.addData(padding);
    }
    return md4.result().left(checksum_bytes);
}

std::expected<ZsyncControlFile, UpdateError> parseZsyncControlFile(const QByteArray& bytes) {
    qsizetype sep = bytes.indexOf("\n\n");
    qsizetype body_offset = sep + 2;
    if (sep < 0) {
        sep = bytes.indexOf("\r\n\r\n");
        body_offset = sep + 4;
    }
    if (sep < 0) {
        return std::unexpected(
            UpdateError{QStringLiteral("zsync_header"),
                        QStringLiteral("Invalid .zsync file: missing header terminator")});
    }

    ZsyncControlFile out;
    const QList<QByteArray> lines = bytes.left(sep).split('\n');
    for (const QByteArray& raw_line : lines) {
        const QByteArray line = raw_line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        const QByteArray key = line.left(colon).trimmed();
        const QByteArray val = line.mid(colon + 1).trimmed();
        if (key == "Filename") {
            out.filename = QString::fromUtf8(val);
        } else if (key == "Blocksize") {
            out.blockSize = val.toInt();
        } else if (key == "Length") {
            out.length = val.toLongLong();
        } else if (key == "SHA-1") {
            out.sha1Hex = QString::fromLatin1(val).toLower();
        } else if (key == "Hash-Lengths") {
            const QList<QByteArray> parts = val.split(',');
            if (parts.size() != 3) {
                return std::unexpected(
                    UpdateError{QStringLiteral("zsync_header"),
                                QStringLiteral("Invalid Hash-Lengths in .zsync")});
            }
            out.seqMatches = parts[0].toInt();
            out.rsumBytes = parts[1].toInt();
            out.checksumBytes = parts[2].toInt();
        }
    }

    if (out.length <= 0 || out.blockSize < 512 || out.blockSize > 65536 ||
        !isPowerOfTwo(out.blockSize)) {
        return std::unexpected(
            UpdateError{QStringLiteral("zsync_header"),
                        QStringLiteral("Unsupported .zsync blocksize or length")});
    }
    if (out.rsumBytes < 1 || out.rsumBytes > 4 || out.checksumBytes < 3 || out.checksumBytes > 16 ||
        out.seqMatches < 1 || out.seqMatches > 2) {
        return std::unexpected(UpdateError{QStringLiteral("zsync_header"),
                                           QStringLiteral("Unsupported .zsync hash lengths")});
    }

    out.blockCount = (out.length + out.blockSize - 1) / out.blockSize;
    const qint64 record_size = static_cast<qint64>(out.rsumBytes + out.checksumBytes);
    const qint64 expected_table_bytes = out.blockCount * record_size;
    if (body_offset + expected_table_bytes > bytes.size()) {
        return std::unexpected(
            UpdateError{QStringLiteral("zsync_truncated"),
                        QStringLiteral("Truncated .zsync block checksum table")});
    }
    out.blockSums = bytes.mid(body_offset, static_cast<qsizetype>(expected_table_bytes));
    return out;
}

std::expected<ZsyncMatchPlan, UpdateError> planZsyncDelta(const ZsyncControlFile& zsync,
                                                          const QString& seed_file_path,
                                                          qint64 coalesce_gap_bytes) {
    ZsyncMatchPlan plan;
    plan.seedBlockOffsets.assign(static_cast<std::size_t>(zsync.blockCount), -1);

    const int record_size = zsync.rsumBytes + zsync.checksumBytes;
    const std::uint32_t mask = rsumMaskForBytes(zsync.rsumBytes);

    // Build weak-rsum hash index mapping masked rsum -> list of target block indices.
    QHash<std::uint32_t, QList<qint64>> rsum_index;
    rsum_index.reserve(static_cast<qsizetype>(zsync.blockCount));
    for (qint64 blk = 0; blk < zsync.blockCount; ++blk) {
        const char* rec = zsync.blockSums.constData() + blk * record_size;
        const std::uint32_t r = readRsumFromTable(rec, zsync.rsumBytes) & mask;
        rsum_index[r].append(blk);
    }

    QFile seed(seed_file_path);
    if (seed.open(QIODevice::ReadOnly) && seed.size() >= zsync.blockSize) {
        const int bs = zsync.blockSize;
        const qint64 chunk_cap = std::max<qint64>(bs * 256LL, 512LL * 1024LL);
        std::vector<std::uint8_t> buf;
        buf.resize(static_cast<std::size_t>(chunk_cap + bs));

        qint64 file_offset = 0;
        std::size_t valid = 0;

        auto match_at = [&](const std::uint8_t* ptr, std::uint32_t masked_rsum,
                            qint64 abs_offset) -> bool {
            const auto it = rsum_index.constFind(masked_rsum);
            if (it == rsum_index.cend()) {
                return false;
            }
            const QByteArray md4 =
                computeZsyncStrongHash(ptr, static_cast<std::size_t>(bs), bs, zsync.checksumBytes);
            bool matched_any = false;
            for (const qint64 blk : it.value()) {
                if (plan.seedBlockOffsets[static_cast<std::size_t>(blk)] >= 0) {
                    continue;
                }
                const char* rec = zsync.blockSums.constData() + blk * record_size;
                if (std::memcmp(rec + zsync.rsumBytes, md4.constData(),
                                static_cast<std::size_t>(zsync.checksumBytes)) == 0) {
                    plan.seedBlockOffsets[static_cast<std::size_t>(blk)] = abs_offset;
                    ++plan.matchedBlocks;
                    matched_any = true;
                }
            }
            return matched_any;
        };

        while (!seed.atEnd() && plan.matchedBlocks < zsync.blockCount) {
            const qint64 to_read = static_cast<qint64>(buf.size() - valid);
            const qint64 got = seed.read(reinterpret_cast<char*>(buf.data() + valid), to_read);
            if (got <= 0) {
                break;
            }
            valid += static_cast<std::size_t>(got);
            if (static_cast<int>(valid) < bs) {
                break;
            }

            std::size_t pos = 0;
            std::uint32_t r = computeZsyncRsum(buf.data(), static_cast<std::size_t>(bs));
            std::uint16_t a = static_cast<std::uint16_t>(r >> 16U);
            std::uint16_t b = static_cast<std::uint16_t>(r & 0xFFFFU);

            while (pos + static_cast<std::size_t>(bs) <= valid &&
                   plan.matchedBlocks < zsync.blockCount) {
                const std::uint32_t cur =
                    ((static_cast<std::uint32_t>(a) << 16U) | static_cast<std::uint32_t>(b)) & mask;
                if (match_at(buf.data() + pos, cur, file_offset + static_cast<qint64>(pos))) {
                    pos += static_cast<std::size_t>(bs);
                    if (pos + static_cast<std::size_t>(bs) <= valid) {
                        r = computeZsyncRsum(buf.data() + pos, static_cast<std::size_t>(bs));
                        a = static_cast<std::uint16_t>(r >> 16U);
                        b = static_cast<std::uint16_t>(r & 0xFFFFU);
                    }
                    continue;
                }
                if (pos + static_cast<std::size_t>(bs) < valid) {
                    const std::uint16_t oldc = buf[pos];
                    const std::uint16_t newc = buf[pos + static_cast<std::size_t>(bs)];
                    a = static_cast<std::uint16_t>(a + newc - oldc);
                    b = static_cast<std::uint16_t>(
                        b + a - static_cast<std::uint16_t>(oldc * static_cast<std::uint16_t>(bs)));
                }
                ++pos;
            }

            const std::size_t carry = valid - pos;
            if (carry > 0 && pos > 0) {
                std::memmove(buf.data(), buf.data() + pos, carry);
            }
            file_offset += static_cast<qint64>(pos);
            valid = carry;
        }
    }

    plan.missingBlocks = zsync.blockCount - plan.matchedBlocks;

    // Build coalesced byte ranges for all missing target blocks.
    qint64 blk = 0;
    while (blk < zsync.blockCount) {
        if (plan.seedBlockOffsets[static_cast<std::size_t>(blk)] >= 0) {
            ++blk;
            continue;
        }
        const qint64 start_byte = blk * zsync.blockSize;
        while (blk < zsync.blockCount && plan.seedBlockOffsets[static_cast<std::size_t>(blk)] < 0) {
            ++blk;
        }
        const qint64 end_byte = std::min(zsync.length - 1, blk * zsync.blockSize - 1);
        if (!plan.missingRanges.isEmpty() &&
            start_byte <= plan.missingRanges.last().end + 1 + coalesce_gap_bytes) {
            plan.missingRanges.last().end = end_byte;
        } else {
            plan.missingRanges.append(HttpRangeRequest{.start = start_byte, .end = end_byte});
        }
    }

    for (const auto& r : plan.missingRanges) {
        plan.downloadBytes += (r.end - r.start + 1);
    }
    return plan;
}

ZsyncDownloader::ZsyncDownloader(QNetworkAccessManager& network, UrlPolicy policy, QObject* parent)
    : QObject(parent), network_(network), policy_(std::move(policy)),
      request_(new UpdateHttpRequest(network_, policy_, this)) {}

ZsyncDownloader::~ZsyncDownloader() {
    cancel();
}

void ZsyncDownloader::start(const UpdateArtifact& appimage_artifact,
                            const QString& seed_appimage_path, const QString& output_path) {
    cancel();
    artifact_ = appimage_artifact;
    seed_path_ = seed_appimage_path;
    output_path_ = output_path;
    temp_path_ = output_path + QStringLiteral(".part");
    active_ = true;
    using_delta_ = false;
    range_index_ = 0;
    completed_bytes_ = 0;
    total_expected_bytes_ = artifact_.size;
    elapsed_.restart();
    last_speed_ms_ = 0;
    last_speed_bytes_ = 0;
    current_speed_bps_ = 0;

    if (!artifact_.isValid()) {
        finishError(QStringLiteral("Invalid AppImage artifact in manifest"));
        return;
    }

    if (artifact_.zsyncUrls.isEmpty() || seed_path_.isEmpty() || !QFileInfo::exists(seed_path_)) {
        startFullDownload();
        return;
    }

    // Fetch .zsync control file first; fall back cleanly to full download on any failure.
    request_->start(artifact_.zsyncUrls, std::nullopt, [this](HttpFetchResult res) {
        if (!active_) {
            return;
        }
        if (!res.ok) {
            startFullDownload();
            return;
        }
        auto parsed = parseZsyncControlFile(res.body);
        if (!parsed || parsed->length != artifact_.size) {
            startFullDownload();
            return;
        }
        auto match_plan = planZsyncDelta(*parsed, seed_path_);
        if (!match_plan || static_cast<double>(match_plan->downloadBytes) >
                               static_cast<double>(artifact_.size) * kZsyncFallbackRatio) {
            startFullDownload();
            return;
        }
        zsync_ = std::move(*parsed);
        plan_ = std::move(*match_plan);
        using_delta_ = true;
        total_expected_bytes_ = plan_.downloadBytes;
        startDeltaAssembly();
    });
}

void ZsyncDownloader::cancel() {
    active_ = false;
    if (request_) {
        request_->cancel();
    }
    if (!temp_path_.isEmpty()) {
        QFile::remove(temp_path_);
    }
}

void ZsyncDownloader::startFullDownload() {
    if (!active_) {
        return;
    }
    using_delta_ = false;
    total_expected_bytes_ = artifact_.size;
    completed_bytes_ = 0;
    QDir().mkpath(QFileInfo(temp_path_).absolutePath());
    QFile::remove(temp_path_);

    auto out_file = std::make_shared<QFile>(temp_path_);
    if (!out_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishError(QStringLiteral("Could not open temporary AppImage file for writing"));
        return;
    }

    request_->start(
        artifact_.urls, std::nullopt,
        [this, out_file](HttpFetchResult res) {
            out_file->close();
            if (!active_) {
                return;
            }
            if (!res.ok) {
                finishError(res.errorMessage);
                return;
            }
            finalizeAndVerify(false);
        },
        [this](qint64 received, qint64 /*total*/) {
            if (active_) {
                updateSpeedAndEmit(received);
            }
        },
        [this, out_file](int /*status*/, const QByteArray& chunk) -> bool {
            if (!active_) {
                return false;
            }
            if (out_file->size() + chunk.size() > artifact_.size) {
                return false;
            }
            return out_file->write(chunk) == chunk.size();
        });
}

void ZsyncDownloader::startDeltaAssembly() {
    QDir().mkpath(QFileInfo(temp_path_).absolutePath());
    QFile::remove(temp_path_);

    QFile out_file(temp_path_);
    if (!out_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !out_file.resize(zsync_.length)) {
        startFullDownload();
        return;
    }

    QFile seed(seed_path_);
    if (!seed.open(QIODevice::ReadOnly)) {
        out_file.close();
        startFullDownload();
        return;
    }

    QByteArray block_buf(zsync_.blockSize, Qt::Uninitialized);
    for (qint64 blk = 0; blk < zsync_.blockCount; ++blk) {
        const qint64 src_off = plan_.seedBlockOffsets[static_cast<std::size_t>(blk)];
        if (src_off < 0) {
            continue;
        }
        const qint64 write_len =
            std::min<qint64>(zsync_.blockSize, zsync_.length - blk * zsync_.blockSize);
        if (!seed.seek(src_off) || seed.read(block_buf.data(), write_len) != write_len ||
            !out_file.seek(blk * zsync_.blockSize) ||
            out_file.write(block_buf.constData(), write_len) != write_len) {
            out_file.close();
            startFullDownload();
            return;
        }
    }
    out_file.close();
    seed.close();

    range_index_ = 0;
    completed_bytes_ = 0;
    fetchNextMissingRange();
}

void ZsyncDownloader::fetchNextMissingRange() {
    if (!active_) {
        return;
    }
    if (range_index_ >= plan_.missingRanges.size()) {
        finalizeAndVerify(true);
        return;
    }
    const HttpRangeRequest range = plan_.missingRanges[range_index_];
    const qint64 expected_len = range.end - range.start + 1;

    request_->start(
        artifact_.urls, range,
        [this, range, expected_len](HttpFetchResult res) {
            if (!active_) {
                return;
            }
            if (!res.ok) {
                // Fall back to full download if range transfer fails.
                startFullDownload();
                return;
            }
            QByteArray slice;
            if (res.httpStatus == 206) {
                slice = res.body;
            } else if (res.httpStatus == 200 && res.body.size() >= range.end + 1) {
                slice = res.body.mid(static_cast<qsizetype>(range.start),
                                     static_cast<qsizetype>(expected_len));
            } else {
                startFullDownload();
                return;
            }
            if (slice.size() != expected_len) {
                startFullDownload();
                return;
            }
            QFile out_file(temp_path_);
            if (!out_file.open(QIODevice::ReadWrite) || !out_file.seek(range.start) ||
                out_file.write(slice) != expected_len) {
                finishError(QStringLiteral("Failed writing zsync delta range to disk"));
                return;
            }
            out_file.close();
            completed_bytes_ += expected_len;
            updateSpeedAndEmit(completed_bytes_);
            ++range_index_;
            fetchNextMissingRange();
        },
        [this](qint64 received, qint64 /*total*/) {
            if (active_) {
                updateSpeedAndEmit(completed_bytes_ + received);
            }
        });
}

void ZsyncDownloader::finalizeAndVerify(bool used_delta) {
    if (QFileInfo(temp_path_).size() != artifact_.size) {
        if (used_delta) {
            startFullDownload();
            return;
        }
        finishError(QStringLiteral("Downloaded AppImage size does not match signed manifest"));
        return;
    }
    const auto actual_sha = computeFileSha256(temp_path_);
    if (!actual_sha || *actual_sha != artifact_.sha256) {
        if (used_delta) {
            // Reconstructed delta failed SHA-256: fall back to full download once.
            startFullDownload();
            return;
        }
        finishError(QStringLiteral("Downloaded AppImage failed Ed25519-signed SHA-256 check"));
        return;
    }
    QFile::remove(output_path_);
    if (!QFile::rename(temp_path_, output_path_)) {
        finishError(QStringLiteral("Could not finalize staged AppImage"));
        return;
    }
    active_ = false;
    emit finished(true, output_path_, used_delta, {});
}

void ZsyncDownloader::updateSpeedAndEmit(qint64 current_downloaded) {
    const qint64 now_ms = elapsed_.elapsed();
    const qint64 dt = now_ms - last_speed_ms_;
    if (dt >= 200) {
        const qint64 delta_bytes = std::max<qint64>(0, current_downloaded - last_speed_bytes_);
        current_speed_bps_ = (delta_bytes * 1000) / dt;
        last_speed_ms_ = now_ms;
        last_speed_bytes_ = current_downloaded;
    } else if (now_ms > 0 && current_speed_bps_ == 0) {
        current_speed_bps_ = (current_downloaded * 1000) / now_ms;
    }
    emit progressChanged(current_downloaded, total_expected_bytes_, current_speed_bps_);
}

void ZsyncDownloader::finishError(const QString& message) {
    if (!active_) {
        return;
    }
    active_ = false;
    if (request_) {
        request_->cancel();
    }
    if (!temp_path_.isEmpty()) {
        QFile::remove(temp_path_);
    }
    emit finished(false, {}, using_delta_, message);
}

} // namespace animehub::update
