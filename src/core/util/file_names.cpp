#include "file_names.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace animehub::util {

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
    result = result.simplified();
    if (result.size() > max_length) {
        result.truncate(max_length);
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
