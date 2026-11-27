#include "ahpack.h"

#include "update_manifest.h"
#include "zstd.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cstring>

namespace animehub::update {
namespace {

constexpr qint64 kShaBufferSize = 64 * 1024;
constexpr qint64 kZstdMaxOutputLimit = 1024LL * 1024LL * 1024LL; // 1 GiB hard safety ceiling

bool verifySha256OfBytes(const QByteArray& data, const QByteArray& expected_hex) {
    const QByteArray actual = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return actual == expected_hex;
}

std::expected<QByteArray, UpdateError> decompressZstdToBytes(const QByteArray& compressed,
                                                             qint64 expected_size) {
    if (expected_size < 0 || expected_size > kZstdMaxOutputLimit) {
        return std::unexpected(
            UpdateError{QStringLiteral("zstd_limit"),
                        QStringLiteral("Decompressed size exceeds safety limit")});
    }
    ZSTD_DStream* dstream = ZSTD_createDStream();
    if (dstream == nullptr) {
        return std::unexpected(UpdateError{QStringLiteral("zstd_init"),
                                           QStringLiteral("Failed to allocate zstd decoder")});
    }
    struct Guard {
        ZSTD_DStream* ptr;
        ~Guard() {
            ZSTD_freeDStream(ptr);
        }
    } guard{dstream};

    const std::size_t init_res = ZSTD_initDStream(dstream);
    if (ZSTD_isError(init_res) != 0U) {
        return std::unexpected(UpdateError{QStringLiteral("zstd_init"),
                                           QString::fromLatin1(ZSTD_getErrorName(init_res))});
    }

    QByteArray out;
    out.reserve(static_cast<qsizetype>(expected_size));
    std::array<char, 64 * 1024> chunk{};

    ZSTD_inBuffer input{compressed.constData(), static_cast<std::size_t>(compressed.size()), 0};
    std::size_t last_ret = 1;
    while (input.pos < input.size) {
        ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
        last_ret = ZSTD_decompressStream(dstream, &output, &input);
        if (ZSTD_isError(last_ret) != 0U) {
            return std::unexpected(UpdateError{QStringLiteral("zstd_decode"),
                                               QString::fromLatin1(ZSTD_getErrorName(last_ret))});
        }
        if (output.pos > 0) {
            if (out.size() + static_cast<qint64>(output.pos) > expected_size) {
                return std::unexpected(
                    UpdateError{QStringLiteral("zstd_size"),
                                QStringLiteral("Decompressed stream exceeded declared size")});
            }
            out.append(chunk.data(), static_cast<qsizetype>(output.pos));
        }
    }
    // Drain any buffered frame tail.
    while (last_ret != 0) {
        ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
        last_ret = ZSTD_decompressStream(dstream, &output, &input);
        if (ZSTD_isError(last_ret) != 0U) {
            return std::unexpected(UpdateError{QStringLiteral("zstd_decode"),
                                               QString::fromLatin1(ZSTD_getErrorName(last_ret))});
        }
        if (output.pos > 0) {
            if (out.size() + static_cast<qint64>(output.pos) > expected_size) {
                return std::unexpected(
                    UpdateError{QStringLiteral("zstd_size"),
                                QStringLiteral("Decompressed stream exceeded declared size")});
            }
            out.append(chunk.data(), static_cast<qsizetype>(output.pos));
        }
        if (output.pos == 0 && last_ret != 0) {
            return std::unexpected(UpdateError{QStringLiteral("zstd_truncated"),
                                               QStringLiteral("Truncated zstd stream")});
        }
    }
    if (out.size() != expected_size) {
        return std::unexpected(
            UpdateError{QStringLiteral("zstd_size"),
                        QStringLiteral("Decompressed size %1 does not match expected %2")
                            .arg(out.size())
                            .arg(expected_size)});
    }
    return out;
}

std::expected<void, UpdateError> decompressZstdToFile(const QByteArray& compressed,
                                                      const QString& target_path,
                                                      qint64 expected_size,
                                                      const QByteArray& expected_sha256) {
    if (expected_size < 0 || expected_size > kZstdMaxOutputLimit) {
        return std::unexpected(
            UpdateError{QStringLiteral("zstd_limit"),
                        QStringLiteral("Decompressed size exceeds safety limit")});
    }
    const QFileInfo fi(target_path);
    if (!QDir().mkpath(fi.path())) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_mkdir"),
                        QStringLiteral("Could not create staging folder '%1'").arg(fi.path())});
    }
    QSaveFile file(target_path);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_open"),
                        QStringLiteral("Could not open '%1' for writing").arg(target_path)});
    }

    ZSTD_DStream* dstream = ZSTD_createDStream();
    if (dstream == nullptr) {
        return std::unexpected(UpdateError{QStringLiteral("zstd_init"),
                                           QStringLiteral("Failed to allocate zstd decoder")});
    }
    struct Guard {
        ZSTD_DStream* ptr;
        ~Guard() {
            ZSTD_freeDStream(ptr);
        }
    } guard{dstream};

    const std::size_t init_res = ZSTD_initDStream(dstream);
    if (ZSTD_isError(init_res) != 0U) {
        return std::unexpected(UpdateError{QStringLiteral("zstd_init"),
                                           QString::fromLatin1(ZSTD_getErrorName(init_res))});
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    qint64 total_written = 0;
    std::array<char, 64 * 1024> chunk{};

    auto flush_chunk = [&](std::size_t n) -> std::expected<void, UpdateError> {
        if (n == 0) {
            return {};
        }
        if (total_written + static_cast<qint64>(n) > expected_size) {
            return std::unexpected(
                UpdateError{QStringLiteral("zstd_size"),
                            QStringLiteral("Decompressed stream exceeded declared size")});
        }
        if (file.write(chunk.data(), static_cast<qint64>(n)) != static_cast<qint64>(n)) {
            return std::unexpected(
                UpdateError{QStringLiteral("io_write"),
                            QStringLiteral("Failed writing staged file '%1'").arg(target_path)});
        }
        hasher.addData(QByteArrayView(chunk.data(), static_cast<qsizetype>(n)));
        total_written += static_cast<qint64>(n);
        return {};
    };

    ZSTD_inBuffer input{compressed.constData(), static_cast<std::size_t>(compressed.size()), 0};
    std::size_t last_ret = 1;
    while (input.pos < input.size) {
        ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
        last_ret = ZSTD_decompressStream(dstream, &output, &input);
        if (ZSTD_isError(last_ret) != 0U) {
            return std::unexpected(UpdateError{QStringLiteral("zstd_decode"),
                                               QString::fromLatin1(ZSTD_getErrorName(last_ret))});
        }
        if (auto res = flush_chunk(output.pos); !res) {
            return res;
        }
    }
    while (last_ret != 0) {
        ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
        last_ret = ZSTD_decompressStream(dstream, &output, &input);
        if (ZSTD_isError(last_ret) != 0U) {
            return std::unexpected(UpdateError{QStringLiteral("zstd_decode"),
                                               QString::fromLatin1(ZSTD_getErrorName(last_ret))});
        }
        if (auto res = flush_chunk(output.pos); !res) {
            return res;
        }
        if (output.pos == 0 && last_ret != 0) {
            return std::unexpected(UpdateError{QStringLiteral("zstd_truncated"),
                                               QStringLiteral("Truncated zstd frame")});
        }
    }
    if (total_written != expected_size) {
        return std::unexpected(
            UpdateError{QStringLiteral("zstd_size"),
                        QStringLiteral("Decompressed size %1 does not match expected %2")
                            .arg(total_written)
                            .arg(expected_size)});
    }
    if (hasher.result().toHex() != expected_sha256) {
        return std::unexpected(
            UpdateError{QStringLiteral("sha256_mismatch"),
                        QStringLiteral("SHA-256 mismatch for '%1'").arg(target_path)});
    }
    if (!file.commit()) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_commit"),
                        QStringLiteral("Could not commit staged file '%1'").arg(target_path)});
    }
    return {};
}

std::expected<void, UpdateError> writeVerifiedFile(const QByteArray& data,
                                                   const QString& target_path, qint64 expected_size,
                                                   const QByteArray& expected_sha256) {
    if (data.size() != expected_size) {
        return std::unexpected(
            UpdateError{QStringLiteral("size_mismatch"),
                        QStringLiteral("Size mismatch for '%1'").arg(target_path)});
    }
    if (!verifySha256OfBytes(data, expected_sha256)) {
        return std::unexpected(
            UpdateError{QStringLiteral("sha256_mismatch"),
                        QStringLiteral("SHA-256 mismatch for '%1'").arg(target_path)});
    }
    const QFileInfo fi(target_path);
    if (!QDir().mkpath(fi.path())) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_mkdir"),
                        QStringLiteral("Could not create directory '%1'").arg(fi.path())});
    }
    QSaveFile file(target_path);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_open"),
                        QStringLiteral("Could not open '%1' for writing").arg(target_path)});
    }
    if (file.write(data) != data.size() || !file.commit()) {
        return std::unexpected(UpdateError{QStringLiteral("io_write"),
                                           QStringLiteral("Failed writing '%1'").arg(target_path)});
    }
    return {};
}

bool isZeroBlock(const char* block) {
    for (int i = 0; i < 512; ++i) {
        if (block[i] != '\0') {
            return false;
        }
    }
    return true;
}

std::optional<qint64> parseTarOctal(const char* field, int len) {
    int i = 0;
    while (i < len && (field[i] == ' ' || field[i] == '\0')) {
        ++i;
    }
    if (i == len) {
        return 0;
    }
    // GNU base-256 high-bit extension (positive integers only).
    if ((static_cast<unsigned char>(field[0]) & 0x80U) != 0U) {
        if ((static_cast<unsigned char>(field[0]) & 0x40U) != 0U) {
            return std::nullopt; // negative
        }
        std::uint64_t val = static_cast<unsigned char>(field[0]) & 0x7FU;
        for (int k = 1; k < len; ++k) {
            val = (val << 8U) | static_cast<unsigned char>(field[k]);
        }
        return static_cast<qint64>(val);
    }
    qint64 val = 0;
    bool saw_digit = false;
    while (i < len && field[i] != '\0' && field[i] != ' ') {
        if (field[i] < '0' || field[i] > '7') {
            return std::nullopt;
        }
        saw_digit = true;
        val = (val * 8) + (field[i] - '0');
        ++i;
    }
    return saw_digit ? std::optional<qint64>(val) : std::nullopt;
}

bool verifyTarHeaderChecksum(const char* header) {
    const auto expected = parseTarOctal(header + 148, 8);
    if (!expected) {
        return false;
    }
    qint64 unsigned_sum = 0;
    for (int i = 0; i < 512; ++i) {
        const unsigned char b = (i >= 148 && i < 156) ? static_cast<unsigned char>(' ')
                                                      : static_cast<unsigned char>(header[i]);
        unsigned_sum += b;
    }
    return unsigned_sum == *expected;
}

QString readTarString(const char* field, int max_len) {
    int n = 0;
    while (n < max_len && field[n] != '\0') {
        ++n;
    }
    return QString::fromUtf8(field, n);
}

QString parsePaxPath(const QByteArray& pax_data) {
    qsizetype pos = 0;
    while (pos < pax_data.size()) {
        const qsizetype sp = pax_data.indexOf(' ', pos);
        if (sp <= pos) {
            break;
        }
        bool ok = false;
        const qsizetype rec_len = pax_data.mid(pos, sp - pos).toLongLong(&ok);
        if (!ok || rec_len <= 0 || pos + rec_len > pax_data.size()) {
            break;
        }
        const QByteArray record = pax_data.mid(sp + 1, pos + rec_len - (sp + 1));
        if (record.startsWith("path=")) {
            QByteArray val = record.mid(5);
            if (val.endsWith('\n')) {
                val.chop(1);
            }
            return QString::fromUtf8(val);
        }
        pos += rec_len;
    }
    return {};
}

std::expected<QList<UpdatePlanFile>, UpdateError> extractSolidTar(const UpdateFileEntry& entry,
                                                                  const QByteArray& compressed,
                                                                  const QString& staging_dir) {
    auto tar_res = decompressZstdToBytes(compressed, entry.size);
    if (!tar_res) {
        return std::unexpected(tar_res.error());
    }
    const QByteArray& tar = *tar_res;
    if (!verifySha256OfBytes(tar, entry.sha256)) {
        return std::unexpected(
            UpdateError{QStringLiteral("sha256_mismatch"),
                        QStringLiteral("SHA-256 mismatch on solid-tar '%1'").arg(entry.path)});
    }
    if (tar.size() % 512 != 0) {
        return std::unexpected(
            UpdateError{QStringLiteral("tar_format"),
                        QStringLiteral("solid-tar '%1' is not 512-byte aligned").arg(entry.path)});
    }

    QHash<QString, UpdateSolidMember> expected_members;
    for (const auto& m : entry.members) {
        expected_members.insert(m.path, m);
    }

    QList<UpdatePlanFile> staged;
    QSet<QString> extracted_paths;
    QString next_pax_path;

    qsizetype pos = 0;
    while (pos + 512 <= tar.size()) {
        const char* hdr = tar.constData() + pos;
        pos += 512;
        if (isZeroBlock(hdr)) {
            break;
        }
        if (!verifyTarHeaderChecksum(hdr)) {
            return std::unexpected(UpdateError{
                QStringLiteral("tar_checksum"),
                QStringLiteral("Invalid USTAR header checksum in '%1'").arg(entry.path)});
        }
        const auto file_size = parseTarOctal(hdr + 124, 12);
        if (!file_size || *file_size < 0) {
            return std::unexpected(
                UpdateError{QStringLiteral("tar_size"),
                            QStringLiteral("Invalid file size in solid-tar '%1'").arg(entry.path)});
        }
        const qsizetype padded = ((*file_size + 511) / 512) * 512;
        if (pos + padded > tar.size()) {
            return std::unexpected(
                UpdateError{QStringLiteral("tar_truncated"),
                            QStringLiteral("Truncated entry in solid-tar '%1'").arg(entry.path)});
        }

        const char typeflag = hdr[156];
        const QByteArray payload = tar.mid(pos, static_cast<qsizetype>(*file_size));
        pos += padded;

        if (typeflag == 'x') {
            next_pax_path = parsePaxPath(payload);
            continue;
        }
        if (typeflag == 'g') {
            continue;
        }
        if (typeflag == '5') {
            next_pax_path.clear();
            continue; // directory entry; parent dirs created automatically
        }
        if (typeflag != '0' && typeflag != '\0') {
            return std::unexpected(
                UpdateError{QStringLiteral("tar_unsafe_type"),
                            QStringLiteral("Rejected non-regular tar entry type '%1' in '%2'")
                                .arg(QLatin1Char(typeflag))
                                .arg(entry.path)});
        }

        QString member_path = next_pax_path;
        next_pax_path.clear();
        if (member_path.isEmpty()) {
            const QString prefix = readTarString(hdr + 345, 155);
            const QString name = readTarString(hdr + 0, 100);
            member_path = prefix.isEmpty() ? name : (prefix + QLatin1Char('/') + name);
        }
        if (!isSafeRelativeUpdatePath(member_path)) {
            return std::unexpected(UpdateError{
                QStringLiteral("unsafe_path"),
                QStringLiteral("Rejected unsafe tar member path '%1'").arg(member_path)});
        }
        const auto it = expected_members.constFind(member_path);
        if (it == expected_members.cend()) {
            return std::unexpected(UpdateError{
                QStringLiteral("tar_unexpected_member"),
                QStringLiteral("Unexpected undeclared file '%1' in solid-tar").arg(member_path)});
        }
        if (extracted_paths.contains(member_path)) {
            return std::unexpected(
                UpdateError{QStringLiteral("tar_duplicate_member"),
                            QStringLiteral("Duplicate file '%1' in solid-tar").arg(member_path)});
        }

        const QString staged_path = QDir(staging_dir).filePath(member_path);
        if (auto written = writeVerifiedFile(payload, staged_path, it->size, it->sha256);
            !written) {
            return std::unexpected(written.error());
        }
        extracted_paths.insert(member_path);
        staged.append(UpdatePlanFile{.relativePath = member_path,
                                     .stagedPath = staged_path,
                                     .sha256 = it->sha256,
                                     .size = it->size,
                                     .executable = false});
    }

    if (extracted_paths.size() != expected_members.size()) {
        return std::unexpected(
            UpdateError{QStringLiteral("tar_missing_members"),
                        QStringLiteral("solid-tar '%1' was missing %2 expected files")
                            .arg(entry.path)
                            .arg(expected_members.size() - extracted_paths.size())});
    }
    return staged;
}

bool localFileMatches(const QString& install_dir, const QString& relative_path, qint64 target_size,
                      const QByteArray& target_sha256,
                      const std::optional<InstalledManifest>& installed) {
    const QString full_path = QDir(install_dir).filePath(relative_path);
    const QFileInfo fi(full_path);
    if (!fi.exists() || !fi.isFile() || fi.isSymLink() || fi.size() != target_size) {
        return false;
    }
    if (installed) {
        const auto it = installed->files.constFind(relative_path);
        if (it != installed->files.cend() && it->size == target_size &&
            it->sha256 == target_sha256 && it->mtimeMs > 0 &&
            it->mtimeMs == fi.lastModified().toMSecsSinceEpoch()) {
            return true;
        }
    }
    const auto actual_sha = computeFileSha256(full_path);
    return actual_sha.has_value() && *actual_sha == target_sha256;
}

} // namespace

std::expected<QByteArray, UpdateError> computeFileSha256(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_open"),
                        QStringLiteral("Could not open '%1' for SHA-256").arg(file_path)});
    }
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    QByteArray buf(kShaBufferSize, Qt::Uninitialized);
    while (!file.atEnd()) {
        const qint64 n = file.read(buf.data(), buf.size());
        if (n < 0) {
            return std::unexpected(
                UpdateError{QStringLiteral("io_read"),
                            QStringLiteral("Read error hashing '%1'").arg(file_path)});
        }
        if (n > 0) {
            hasher.addData(QByteArrayView(buf.constData(), static_cast<qsizetype>(n)));
        }
    }
    return hasher.result().toHex();
}

std::optional<InstalledManifest> loadInstalledManifest(const QString& manifest_path) {
    QFile file(manifest_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        return std::nullopt;
    }
    InstalledManifest out;
    out.version = root.value(QStringLiteral("version")).toString();
    out.releaseSeq = static_cast<std::uint64_t>(
        std::max<qint64>(0, root.value(QStringLiteral("releaseSeq")).toInteger(0)));
    const QJsonObject files_obj = root.value(QStringLiteral("files")).toObject();
    for (auto it = files_obj.begin(); it != files_obj.end(); ++it) {
        if (!isSafeRelativeUpdatePath(it.key()) || !it.value().isObject()) {
            continue;
        }
        const QJsonObject rec_obj = it.value().toObject();
        const QByteArray sha =
            normalizeSha256Hex(rec_obj.value(QStringLiteral("sha256")).toString());
        if (sha.isEmpty()) {
            continue;
        }
        InstalledFileRecord rec;
        rec.size = rec_obj.value(QStringLiteral("size")).toInteger(0);
        rec.sha256 = sha;
        rec.mtimeMs = rec_obj.value(QStringLiteral("mtimeMs")).toInteger(0);
        out.files.insert(it.key(), rec);
    }
    return out;
}

std::expected<void, UpdateError> writeInstalledManifestForTarget(const QString& output_path,
                                                                 const UpdateManifest& manifest,
                                                                 const QString& install_dir,
                                                                 const QString& staging_dir) {
    QJsonObject files_obj;
    auto record_file = [&](const QString& rel_path, qint64 size, const QByteArray& sha256) {
        qint64 mtime_ms = 0;
        if (!staging_dir.isEmpty()) {
            const QFileInfo sfi(QDir(staging_dir).filePath(rel_path));
            if (sfi.exists()) {
                mtime_ms = sfi.lastModified().toMSecsSinceEpoch();
            }
        }
        if (mtime_ms == 0 && !install_dir.isEmpty()) {
            const QFileInfo ifi(QDir(install_dir).filePath(rel_path));
            if (ifi.exists()) {
                mtime_ms = ifi.lastModified().toMSecsSinceEpoch();
            }
        }
        QJsonObject rec;
        rec.insert(QStringLiteral("size"), size);
        rec.insert(QStringLiteral("sha256"), QString::fromLatin1(sha256));
        if (mtime_ms > 0) {
            rec.insert(QStringLiteral("mtimeMs"), mtime_ms);
        }
        files_obj.insert(rel_path, rec);
    };

    for (const auto& entry : manifest.files) {
        if (entry.kind == QLatin1StringView("solid-tar")) {
            for (const auto& m : entry.members) {
                record_file(m.path, m.size, m.sha256);
            }
        } else {
            record_file(entry.path, entry.size, entry.sha256);
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("version"), manifest.version.toString());
    root.insert(QStringLiteral("releaseSeq"), static_cast<qint64>(manifest.releaseSeq));
    root.insert(QStringLiteral("files"), files_obj);

    const QFileInfo fi(output_path);
    if (!QDir().mkpath(fi.path())) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_mkdir"),
                        QStringLiteral("Could not create folder for installed-manifest.json")});
    }
    QSaveFile out(output_path);
    if (!out.open(QIODevice::WriteOnly)) {
        return std::unexpected(
            UpdateError{QStringLiteral("io_open"),
                        QStringLiteral("Could not open '%1' for writing").arg(output_path)});
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (out.write(bytes) != bytes.size() || !out.commit()) {
        return std::unexpected(UpdateError{QStringLiteral("io_write"),
                                           QStringLiteral("Failed writing '%1'").arg(output_path)});
    }
    return {};
}

std::expected<AhpackDiff, UpdateError> computeAhpackDiff(const QString& install_dir,
                                                         const UpdateManifest& manifest) {
    if (manifest.files.isEmpty()) {
        return std::unexpected(
            UpdateError{QStringLiteral("empty_manifest"),
                        QStringLiteral("Manifest contains no file entries for delta update")});
    }
    const auto installed = loadInstalledManifest(
        QDir(install_dir).filePath(QStringLiteral("installed-manifest.json")));

    AhpackDiff diff;
    QSet<QString> desired_paths_lower;

    for (const auto& entry : manifest.files) {
        if (entry.kind == QLatin1StringView("solid-tar")) {
            bool any_changed = false;
            for (const auto& member : entry.members) {
                desired_paths_lower.insert(member.path.toLower());
                if (!localFileMatches(install_dir, member.path, member.size, member.sha256,
                                      installed)) {
                    any_changed = true;
                } else {
                    ++diff.unchangedFileCount;
                }
            }
            if (any_changed) {
                diff.entriesToDownload.append(entry);
            }
        } else {
            desired_paths_lower.insert(entry.path.toLower());
            if (!localFileMatches(install_dir, entry.path, entry.size, entry.sha256, installed)) {
                diff.entriesToDownload.append(entry);
            } else {
                ++diff.unchangedFileCount;
            }
        }
    }

    std::ranges::sort(diff.entriesToDownload,
                      [](const UpdateFileEntry& a, const UpdateFileEntry& b) {
                          return a.packOffset < b.packOffset;
                      });

    diff.coalescedRanges = coalesceByteRanges(diff.entriesToDownload);
    for (const auto& r : diff.coalescedRanges) {
        diff.downloadBytes += r.length();
    }

    if (installed) {
        for (auto it = installed->files.cbegin(); it != installed->files.cend(); ++it) {
            if (!desired_paths_lower.contains(it.key().toLower()) &&
                isSafeRelativeUpdatePath(it.key())) {
                if (QFileInfo::exists(QDir(install_dir).filePath(it.key()))) {
                    diff.filesToDelete.append(it.key());
                }
            }
        }
        diff.filesToDelete.sort();
    }
    return diff;
}

QList<ByteRange> coalesceByteRanges(const QList<UpdateFileEntry>& entries, qint64 gap_bytes) {
    if (entries.isEmpty()) {
        return {};
    }
    QList<ByteRange> raw;
    raw.reserve(entries.size());
    for (const auto& entry : entries) {
        if (entry.packSize <= 0) {
            continue;
        }
        raw.append(
            ByteRange{.start = entry.packOffset, .end = entry.packOffset + entry.packSize - 1});
    }
    if (raw.isEmpty()) {
        return {};
    }
    std::ranges::sort(raw, [](const ByteRange& a, const ByteRange& b) {
        return a.start < b.start || (a.start == b.start && a.end < b.end);
    });

    QList<ByteRange> merged;
    merged.append(raw.first());
    for (qsizetype i = 1; i < raw.size(); ++i) {
        ByteRange& tail = merged.last();
        if (raw[i].start <= tail.end + 1 + gap_bytes) {
            tail.end = std::max(tail.end, raw[i].end);
        } else {
            merged.append(raw[i]);
        }
    }
    return merged;
}

std::expected<QList<UpdatePlanFile>, UpdateError> extractAhpackEntry(const UpdateFileEntry& entry,
                                                                     const QByteArray& packed_bytes,
                                                                     const QString& staging_dir) {
    if (packed_bytes.size() != entry.packSize) {
        return std::unexpected(
            UpdateError{QStringLiteral("pack_slice_size"),
                        QStringLiteral("Pack slice size %1 does not match expected %2 for '%3'")
                            .arg(packed_bytes.size())
                            .arg(entry.packSize)
                            .arg(entry.path)});
    }
    if (entry.kind == QLatin1StringView("solid-tar")) {
        return extractSolidTar(entry, packed_bytes, staging_dir);
    }

    const QString staged_path = QDir(staging_dir).filePath(entry.path);
    if (entry.encoding == QLatin1StringView("raw")) {
        if (auto res = writeVerifiedFile(packed_bytes, staged_path, entry.size, entry.sha256);
            !res) {
            return std::unexpected(res.error());
        }
    } else if (entry.encoding == QLatin1StringView("zstd")) {
        if (auto res = decompressZstdToFile(packed_bytes, staged_path, entry.size, entry.sha256);
            !res) {
            return std::unexpected(res.error());
        }
    } else {
        return std::unexpected(
            UpdateError{QStringLiteral("unsupported_encoding"),
                        QStringLiteral("Unsupported entry encoding '%1'").arg(entry.encoding)});
    }
    return QList<UpdatePlanFile>{UpdatePlanFile{.relativePath = entry.path,
                                                .stagedPath = staged_path,
                                                .sha256 = entry.sha256,
                                                .size = entry.size,
                                                .executable = entry.executable}};
}

AhpackDownloader::AhpackDownloader(QNetworkAccessManager& network, UrlPolicy policy,
                                   QObject* parent)
    : QObject(parent), network_(network), policy_(std::move(policy)),
      request_(new UpdateHttpRequest(network_, policy_, this)) {}

AhpackDownloader::~AhpackDownloader() {
    cancel();
}

void AhpackDownloader::start(const UpdateManifest& manifest, const AhpackDiff& diff,
                             const QString& staging_dir) {
    cancel();
    if (!manifest.pack || !manifest.pack->isValid()) {
        emit finished(false, {}, QStringLiteral("Manifest has no valid .ahpack artifact"));
        return;
    }
    pack_urls_ = manifest.pack->urls;
    pack_total_size_ = manifest.pack->size;
    entries_ = diff.entriesToDownload;
    ranges_ = diff.coalescedRanges;
    staging_dir_ = staging_dir;
    staged_files_.clear();
    range_index_ = 0;
    completed_range_bytes_ = 0;
    total_expected_bytes_ = diff.downloadBytes;
    full_stream_mode_ = false;
    stream_pos_ = 0;
    stream_entry_index_ = 0;
    stream_entry_buf_.clear();
    stream_error_.clear();
    active_ = true;
    elapsed_.restart();
    last_speed_ms_ = 0;
    last_speed_bytes_ = 0;
    current_speed_bps_ = 0;

    if (!QDir().mkpath(staging_dir_)) {
        finishError(QStringLiteral("Could not create staging directory"));
        return;
    }
    if (entries_.isEmpty() || ranges_.isEmpty()) {
        finishSuccess();
        return;
    }
    fetchNextRange();
}

void AhpackDownloader::cancel() {
    active_ = false;
    if (request_) {
        request_->cancel();
    }
}

void AhpackDownloader::fetchNextRange() {
    if (!active_) {
        return;
    }
    if (range_index_ >= ranges_.size()) {
        finishSuccess();
        return;
    }
    const ByteRange range = ranges_[range_index_];
    auto current_range_buf = std::make_shared<QByteArray>();
    current_range_buf->reserve(static_cast<qsizetype>(range.length()));

    request_->start(
        pack_urls_, HttpRangeRequest{.start = range.start, .end = range.end},
        [this, range, current_range_buf](HttpFetchResult res) {
            if (!active_) {
                return;
            }
            if (!res.ok) {
                finishError(res.errorMessage);
                return;
            }
            if (full_stream_mode_) {
                if (!stream_error_.isEmpty()) {
                    finishError(stream_error_);
                    return;
                }
                if (stream_entry_index_ < entries_.size()) {
                    finishError(QStringLiteral("Incomplete 200 OK pack stream"));
                    return;
                }
                finishSuccess();
                return;
            }
            handleRangeComplete(range, *current_range_buf);
        },
        [this](qint64 received, qint64 /*total*/) {
            if (!active_) {
                return;
            }
            if (full_stream_mode_) {
                updateSpeedAndEmit(received);
            } else {
                updateSpeedAndEmit(completed_range_bytes_ + received);
            }
        },
        [this, range, current_range_buf](int http_status, const QByteArray& chunk) -> bool {
            if (!active_) {
                return false;
            }
            if (http_status == 200) {
                // Server/proxy stripped Range header: seamlessly switch to single-pass full stream!
                if (!full_stream_mode_) {
                    full_stream_mode_ = true;
                    total_expected_bytes_ = pack_total_size_;
                    stream_pos_ = 0;
                    stream_entry_index_ = 0;
                    stream_entry_buf_.clear();
                    staged_files_.clear();
                }
                return handleFullStreamChunk(chunk);
            }
            if (current_range_buf->size() + chunk.size() > range.length()) {
                stream_error_ = QStringLiteral("HTTP 206 body exceeded requested range length");
                return false;
            }
            current_range_buf->append(chunk);
            return true;
        });
}

void AhpackDownloader::handleRangeComplete(const ByteRange& range, const QByteArray& bytes) {
    if (bytes.size() != range.length()) {
        finishError(QStringLiteral("HTTP 206 returned %1 bytes, expected %2")
                        .arg(bytes.size())
                        .arg(range.length()));
        return;
    }
    for (const auto& entry : entries_) {
        if (entry.packOffset >= range.start && entry.packOffset + entry.packSize - 1 <= range.end) {
            const qsizetype rel = static_cast<qsizetype>(entry.packOffset - range.start);
            const QByteArray slice = bytes.mid(rel, static_cast<qsizetype>(entry.packSize));
            extractEntryFromBuffer(entry, slice);
            if (!active_) {
                return;
            }
        }
    }
    completed_range_bytes_ += range.length();
    updateSpeedAndEmit(completed_range_bytes_);
    ++range_index_;
    fetchNextRange();
}

bool AhpackDownloader::handleFullStreamChunk(const QByteArray& chunk) {
    const qint64 chunk_start = stream_pos_;
    const qint64 chunk_end = stream_pos_ + chunk.size();
    stream_pos_ = chunk_end;

    while (stream_entry_index_ < entries_.size()) {
        const auto& entry = entries_[stream_entry_index_];
        const qint64 entry_start = entry.packOffset;
        const qint64 entry_end = entry.packOffset + entry.packSize;

        if (chunk_end <= entry_start) {
            break; // haven't reached this entry yet
        }
        if (chunk_start >= entry_end) {
            // Should not happen since entries are sorted, safeguard.
            stream_error_ = QStringLiteral("Internal stream offset alignment error");
            return false;
        }
        const qint64 overlap_start = std::max(chunk_start, entry_start);
        const qint64 overlap_end = std::min(chunk_end, entry_end);
        const qsizetype offset_in_chunk = static_cast<qsizetype>(overlap_start - chunk_start);
        const qsizetype count = static_cast<qsizetype>(overlap_end - overlap_start);
        stream_entry_buf_.append(chunk.constData() + offset_in_chunk, count);

        if (stream_entry_buf_.size() == entry.packSize) {
            extractEntryFromBuffer(entry, stream_entry_buf_);
            stream_entry_buf_.clear();
            if (!active_) {
                return false;
            }
            ++stream_entry_index_;
        } else {
            break; // need more chunks for this entry
        }
    }
    if (stream_entry_index_ >= entries_.size()) {
        // All needed entries extracted from the 200 OK stream — stop reading early!
        return false;
    }
    return true;
}

void AhpackDownloader::extractEntryFromBuffer(const UpdateFileEntry& entry,
                                              const QByteArray& bytes) {
    auto extracted = extractAhpackEntry(entry, bytes, staging_dir_);
    if (!extracted) {
        stream_error_ = extracted.error().message;
        finishError(extracted.error().message);
        return;
    }
    staged_files_.append(*extracted);
}

void AhpackDownloader::updateSpeedAndEmit(qint64 current_downloaded) {
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

void AhpackDownloader::finishSuccess() {
    active_ = false;
    auto out = std::move(staged_files_);
    staged_files_.clear();
    emit finished(true, out, {});
}

void AhpackDownloader::finishError(const QString& message) {
    if (!active_) {
        return;
    }
    active_ = false;
    if (request_) {
        request_->cancel();
    }
    emit finished(false, {}, message);
}

} // namespace animehub::update
