#include "file_names.h"

#include "library/library_types.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace animehub::util {

namespace {

// Cut to `max_length` without leaving half a surrogate pair.
QString cleanName(const QString& name, const QString& fallback, int max_length) {
    auto result = safeFileName(name, fallback, max_length);
    if (result.back().isHighSurrogate())
        result.chop(1);
    return result;
}

QString slugHash(const QString& slug, int length) {
    return QString::fromLatin1(
        QCryptographicHash::hash(slug.toUtf8(), QCryptographicHash::Sha256).toHex().left(length));
}

} // namespace

QString workFolderName(const QString& title, std::optional<int> year, const QString& slug,
                       int attempt) {
    QString suffix;
    if (attempt > 0 && year)
        suffix = QStringLiteral(" (%1)").arg(*year);
    if (attempt > 1 || (attempt == 1 && !year))
        suffix += QStringLiteral(" [") + slugHash(slug, 6) + u']';
    return cleanName(title, QStringLiteral("Anime"), 80 - static_cast<int>(suffix.size())) + suffix;
}

QString legacyWorkFolderName(const QString& slug) {
    static const QRegularExpression kCanonicalSlug(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    if (slug.size() <= 80 && kCanonicalSlug.match(slug).hasMatch() && safeFileName(slug) == slug)
        return slug;
    return cleanName(slug, QStringLiteral("anime"), 62) + u'~' + slugHash(slug, 16);
}

QString downloadBasePath(const QString& folder, const QString& anime_title, double number) {
    auto label = library::formatEpisodeNumber(number);
    const auto dot = label.indexOf(u'.');
    const auto whole = dot < 0 ? label.size() : dot;
    if (whole < 2)
        label.prepend(QString(2 - whole, u'0'));
    const auto suffix = QStringLiteral(" - ") + label;
    const auto title =
        cleanName(anime_title, QStringLiteral("Anime"), 119 - static_cast<int>(suffix.size()));
    return QDir(folder).filePath(title + suffix);
}

QString safeFileName(const QString& name, const QString& fallback, int max_length) {
    QString result;
    result.reserve(name.size());
    for (const auto ch : name) {
        if (ch.unicode() < 32 || QStringView(u"<>:\"/\\|?*").contains(ch)) {
            result.append(u' ');
        } else {
            result.append(ch);
        }
    }
    // A character dropped before a closing bracket ("Nakatta!?)") leaves no gap behind.
    static const QRegularExpression kGapBeforeClose(QStringLiteral(" +([)\\]}])"));
    result = result.simplified().replace(kGapBeforeClose, QStringLiteral("\\1"));
    if (result.size() > max_length) {
        // At the last whole word, unless that loses more than half of what fits.
        const auto space = result.lastIndexOf(u' ', max_length);
        result.truncate(space > max_length / 2 ? space : max_length);
    }
    while (!result.isEmpty() && (result.endsWith(u'.') || result.endsWith(u' '))) {
        result.chop(1);
    }
    static const QRegularExpression kReserved(
        QStringLiteral("^(con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (kReserved.match(result).hasMatch()) {
        result.prepend(u'_');
    }
    if (result.isEmpty() || result == u"." || result == u"..") {
        return fallback;
    }
    return result;
}

QString videoExtension(const QString& provider_name) {
    static const QRegularExpression kExtension(QStringLiteral("\\.([A-Za-z0-9]{2,4})$"));
    const auto match = kExtension.match(provider_name);
    if (!match.hasMatch()) {
        return QStringLiteral(".mp4");
    }
    const auto extension = match.captured(1).toLower();
    static const QStringList kVideo = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("webm"),
        QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("ts")};
    return kVideo.contains(extension) ? QLatin1Char('.') + extension : QStringLiteral(".mp4");
}

QString uniquePath(const QString& path) {
    if (!QFileInfo::exists(path)) {
        return path;
    }
    const QFileInfo info(path);
    const auto base = info.completeBaseName();
    const auto suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    for (int index = 2; index < 1000; ++index) {
        const auto candidate =
            info.dir().filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(index).arg(suffix));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool isInside(const QString& path, const QString& directory) {
    const auto clean_dir = QDir::cleanPath(QDir(directory).absolutePath());
    const auto clean_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto sensitivity = Qt::CaseInsensitive; // Windows and macOS default file systems
    return clean_path.compare(clean_dir, sensitivity) == 0 ||
           clean_path.startsWith(clean_dir + QLatin1Char('/'), sensitivity);
}

} // namespace animehub::util
