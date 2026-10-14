#include "display.h"

#include <QLocale>

#include <cmath>

namespace animehub::app::display {
namespace {

const QLocale& spanish() {
    static const QLocale locale(QLocale::Spanish, QLocale::Spain);
    return locale;
}

} // namespace

QString episodeNumber(double number) {
    if (number == std::trunc(number)) {
        return QString::number(static_cast<qint64>(number));
    }
    return spanish().toString(number, 'g', 6);
}

QString episode(double number) {
    return QStringLiteral("Episodio ") + episodeNumber(number);
}

QString bytes(qint64 value) {
    if (value < 1000 * 1000) {
        return spanish().toString(std::max<qint64>(value, 0) / 1000) + QStringLiteral(" KB");
    }
    if (value < 1000LL * 1000 * 1000) {
        return spanish().toString(value / (1000 * 1000)) + QStringLiteral(" MB");
    }
    return spanish().toString(static_cast<double>(value) / 1e9, 'f', 1) + QStringLiteral(" GB");
}

QString speed(qint64 bytes_per_second) {
    if (bytes_per_second < 1000 * 1000) {
        return spanish().toString(bytes_per_second / 1000) + QStringLiteral(" KB/s");
    }
    return spanish().toString(static_cast<double>(bytes_per_second) / 1e6, 'f', 1) +
           QStringLiteral(" MB/s");
}

QString clock(qint64 ms) {
    const qint64 total = std::max<qint64>(ms, 0) / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString remaining(qint64 seconds) {
    if (seconds < 60) {
        return QStringLiteral("menos de un minuto");
    }
    if (seconds < 3600) {
        return QStringLiteral("%1 min").arg((seconds + 59) / 60);
    }
    return QStringLiteral("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

QString status(api::AnimeStatus status) {
    switch (status) {
    case api::AnimeStatus::Airing:
        return QStringLiteral("En emisión");
    case api::AnimeStatus::Finished:
        return QStringLiteral("Finalizado");
    case api::AnimeStatus::Upcoming:
        return QStringLiteral("Próximamente");
    case api::AnimeStatus::Unknown:
        break;
    }
    return {};
}

QString provider(api::Provider provider) {
    return api::providerLabel(provider);
}

QString issue(downloads::TaskIssue issue) {
    using downloads::TaskIssue;
    switch (issue) {
    case TaskIssue::None:
        return {};
    case TaskIssue::NoSource:
        return QStringLiteral("Ni MEGA ni Pixeldrain tienen este episodio.");
    case TaskIssue::SourceRemoved:
        return QStringLiteral("El servidor ya no tiene este archivo.");
    case TaskIssue::Quota:
        return QStringLiteral("Se agotó la cuota de descarga de MEGA.");
    case TaskIssue::RateLimited:
        return QStringLiteral("AnimeHub pidió esperar un momento.");
    case TaskIssue::Network:
        return QStringLiteral("No hay conexión con el servidor.");
    case TaskIssue::NoSpace:
        return QStringLiteral("No queda espacio en el disco.");
    case TaskIssue::Storage:
        return QStringLiteral("No se puede escribir en la carpeta de descargas.");
    case TaskIssue::Integrity:
        return QStringLiteral("El archivo llegó dañado varias veces.");
    case TaskIssue::Engine:
        return QStringLiteral("El motor de descargas se detuvo.");
    }
    return {};
}

} // namespace animehub::app::display
