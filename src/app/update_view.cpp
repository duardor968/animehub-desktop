#include "update_view.h"

#include "display.h"

#include <QDateTime>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace animehub::app {
namespace {

const auto kIconUpdate = QStringLiteral("e777");
const auto kIconDownload = QStringLiteral("e896");

QUrl fallbackReleasesUrl() {
#ifdef ANIMEHUB_HOMEPAGE_URL
    return QUrl(QStringLiteral(ANIMEHUB_HOMEPAGE_URL) + QStringLiteral("/releases/latest"));
#else
    return QUrl(QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/latest"));
#endif
}

QString modeDisplayName(AppSettings::UpdateMode mode) {
    switch (mode) {
    case AppSettings::UpdateMode::Auto:
        return QStringLiteral("Automáticas");
    case AppSettings::UpdateMode::NotifyOnly:
        return QStringLiteral("Solo avisar");
    case AppSettings::UpdateMode::Manual:
        return QStringLiteral("Manuales");
    }
    return QStringLiteral("Automáticas");
}

QString formatErrorSummary(const QString& raw) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("No se pudo comprobar");
    }
    const QString lower = trimmed.toLower();
    if (lower.contains(QLatin1StringView("sin conexión")) ||
        lower.contains(QLatin1StringView("host")) ||
        lower.contains(QLatin1StringView("connection")) ||
        lower.contains(QLatin1StringView("network")) ||
        lower.contains(QLatin1StringView("timeout")) ||
        lower.contains(QLatin1StringView("timed out")) ||
        lower.contains(QLatin1StringView("unreachable"))) {
        return QStringLiteral("Sin conexión");
    }
    return trimmed;
}

AppSettings::UpdateMode parsePreviewMode(const QString& mode, AppSettings::UpdateMode fallback) {
    if (mode == u"auto") {
        return AppSettings::UpdateMode::Auto;
    }
    if (mode == u"notify") {
        return AppSettings::UpdateMode::NotifyOnly;
    }
    if (mode == u"manual") {
        return AppSettings::UpdateMode::Manual;
    }
    return fallback;
}

} // namespace

UpdateView::UpdateView(update::UpdateService& service, AppSettings& settings,
                       std::function<void(const QUrl&)> open_url,
                       std::function<int()> active_downloads, QObject* parent)
    : QObject(parent), service_(service), settings_(settings), open_url_(std::move(open_url)),
      active_downloads_(std::move(active_downloads)) {
    connect(&service_, &update::UpdateService::changed, this, &UpdateView::changed);
    connect(&settings_, &AppSettings::changed, this, &UpdateView::changed);
}

update::UpdateState UpdateView::effectiveState() const {
    return preview_ ? preview_->state : service_.state();
}

AppSettings::UpdateMode UpdateView::effectiveMode() const {
    return preview_ ? preview_->mode : settings_.updateMode();
}

update::CheckSource UpdateView::effectiveSource() const {
    return preview_ ? preview_->source : service_.lastCheckSource();
}

qint64 UpdateView::effectiveDownloadedBytes() const {
    return preview_ ? preview_->downloadedBytes : service_.downloadedBytes();
}

qint64 UpdateView::effectiveTotalBytes() const {
    return preview_ ? preview_->totalBytes : service_.totalBytes();
}

qint64 UpdateView::effectiveSpeedBytesPerSec() const {
    return preview_ ? preview_->speedBytesPerSec : service_.speedBytesPerSec();
}

qint64 UpdateView::effectiveLastCheckedAtMs() const {
    return preview_ ? preview_->lastCheckedAtMs : service_.lastCheckedAtMs();
}

QString UpdateView::effectiveErrorMessage() const {
    return preview_ ? preview_->errorMessage : service_.errorMessage();
}

QString UpdateView::state() const {
    switch (effectiveState()) {
    case update::UpdateState::Idle:
        return QStringLiteral("uptodate");
    case update::UpdateState::Checking:
        return QStringLiteral("checking");
    case update::UpdateState::Available:
        return QStringLiteral("available");
    case update::UpdateState::Downloading:
        return QStringLiteral("downloading");
    case update::UpdateState::Verifying:
        return QStringLiteral("verifying");
    case update::UpdateState::Ready:
        return QStringLiteral("ready");
    case update::UpdateState::Applying:
        return QStringLiteral("applying");
    case update::UpdateState::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("uptodate");
}

bool UpdateView::checking() const {
    return effectiveState() == update::UpdateState::Checking;
}

bool UpdateView::busy() const {
    const auto st = effectiveState();
    return st == update::UpdateState::Checking || st == update::UpdateState::Downloading ||
           st == update::UpdateState::Verifying || st == update::UpdateState::Applying;
}

bool UpdateView::available() const {
    return effectiveState() == update::UpdateState::Available;
}

bool UpdateView::downloading() const {
    const auto st = effectiveState();
    return st == update::UpdateState::Downloading || st == update::UpdateState::Verifying;
}

bool UpdateView::ready() const {
    return effectiveState() == update::UpdateState::Ready;
}

bool UpdateView::error() const {
    return effectiveState() == update::UpdateState::Error;
}

bool UpdateView::majorUpdate() const {
    return preview_ ? preview_->majorUpdate : service_.isMajorUpdate();
}

bool UpdateView::manualOnly() const {
    return preview_ ? preview_->manualOnly : service_.requiresManualUpdate();
}

QString UpdateView::targetVersion() const {
    return preview_ ? preview_->targetVersion : service_.availableVersion();
}

QString UpdateView::notes() const {
    return preview_ ? preview_->notes : service_.releaseNotes().trimmed();
}

QUrl UpdateView::releaseUrl() const {
    return preview_ ? preview_->releaseUrl : service_.releaseUrl();
}

QUrl UpdateView::manualDownloadUrl() const {
    if (preview_) {
        return preview_->manualDownloadUrl.isValid() ? preview_->manualDownloadUrl
                                                     : fallbackReleasesUrl();
    }
    if (const auto& manifest = service_.availableManifest(); manifest.has_value()) {
        if (service_.installMode() == update::InstallMode::LinuxDeb && manifest->deb &&
            !manifest->deb->urls.isEmpty()) {
            return manifest->deb->urls.first();
        }
        if (manifest->requiresFullInstaller && manifest->installer &&
            !manifest->installer->urls.isEmpty()) {
            return manifest->installer->urls.first();
        }
        if (manifest->releaseUrl.isValid()) {
            return manifest->releaseUrl;
        }
    }
    return fallbackReleasesUrl();
}

double UpdateView::progress() const {
    const auto st = effectiveState();
    if (st == update::UpdateState::Verifying || st == update::UpdateState::Ready ||
        st == update::UpdateState::Applying) {
        return 1.0;
    }
    if (st != update::UpdateState::Downloading) {
        return 0.0;
    }
    const qint64 total = effectiveTotalBytes();
    if (total <= 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(effectiveDownloadedBytes()) / static_cast<double>(total),
                      0.0, 1.0);
}

int UpdateView::progressPercent() const {
    return std::clamp(static_cast<int>(std::lround(progress() * 100.0)), 0, 100);
}

QString UpdateView::sizeText() const {
    const qint64 total = effectiveTotalBytes();
    return total > 0 ? display::bytes(total) : QString();
}

QString UpdateView::progressDetailText() const {
    const auto st = effectiveState();
    if (st == update::UpdateState::Verifying) {
        return QStringLiteral("Verificando…");
    }
    if (st != update::UpdateState::Downloading) {
        return {};
    }
    const qint64 done = std::max<qint64>(0, effectiveDownloadedBytes());
    const qint64 total = effectiveTotalBytes();
    const qint64 speed = effectiveSpeedBytesPerSec();

    QStringList parts;
    if (total > 0) {
        parts.append(display::bytes(done) + QStringLiteral(" de ") + display::bytes(total));
    } else if (done > 0) {
        parts.append(display::bytes(done));
    }
    if (speed > 0) {
        parts.append(display::speed(speed));
    }
    if (total > 0) {
        parts.append(QStringLiteral("%1 %").arg(progressPercent()));
    }
    return parts.join(QStringLiteral(" · "));
}

QString UpdateView::lastCheckedText() const {
    const QString mode_name = modeDisplayName(effectiveMode());
    const qint64 checked_ms = effectiveLastCheckedAtMs();
    if (checked_ms <= 0) {
        return QStringLiteral("Sin comprobar todavía · %1").arg(mode_name);
    }
    const QDateTime at = QDateTime::fromMSecsSinceEpoch(checked_ms);
    const QDateTime now = QDateTime::currentDateTime();
    if (at.toLocalTime().date() == now.toLocalTime().date()) {
        return QStringLiteral("Última comprobación: hoy a las %1 · %2")
            .arg(display::time(at), mode_name);
    }
    return QStringLiteral("Última comprobación: %1 · %2").arg(display::when(at, now), mode_name);
}

QString UpdateView::readySubtitleText() const {
    if (effectiveMode() == AppSettings::UpdateMode::Auto && !majorUpdate() &&
        effectiveSource() == update::CheckSource::Api) {
        return QStringLiteral("Se instalará al reiniciar o al salir");
    }
    return QStringLiteral("Lista para instalar al reiniciar");
}

QString UpdateView::errorDetailText() const {
    return QStringLiteral("%1 · Sigues en la versión %2")
        .arg(formatErrorSummary(effectiveErrorMessage()), service_.currentVersion());
}

bool UpdateView::pillVisible() const {
    const auto mode = effectiveMode();
    if (mode == AppSettings::UpdateMode::Manual) {
        return false;
    }
    const auto st = effectiveState();
    if (mode == AppSettings::UpdateMode::Auto) {
        if (st == update::UpdateState::Ready) {
            return true;
        }
        return st == update::UpdateState::Available && (majorUpdate() || manualOnly());
    }
    // NotifyOnly
    return st == update::UpdateState::Available || st == update::UpdateState::Downloading ||
           st == update::UpdateState::Verifying || st == update::UpdateState::Ready;
}

QString UpdateView::pillIcon() const {
    const auto st = effectiveState();
    if (st == update::UpdateState::Ready) {
        return kIconUpdate;
    }
    if (st == update::UpdateState::Downloading || st == update::UpdateState::Verifying) {
        return kIconDownload;
    }
    if (st == update::UpdateState::Available) {
        return (manualOnly() || majorUpdate()) ? kIconUpdate : kIconDownload;
    }
    return kIconUpdate;
}

QString UpdateView::pillLabel() const {
    const QString ver = targetVersion();
    const auto st = effectiveState();
    if (st == update::UpdateState::Ready) {
        return QStringLiteral("Actualizar a %1").arg(ver);
    }
    if (st == update::UpdateState::Verifying) {
        return QStringLiteral("Verificando %1…").arg(ver);
    }
    if (st == update::UpdateState::Downloading) {
        if (effectiveTotalBytes() > 0) {
            return QStringLiteral("Descargando %1 · %2 %").arg(ver).arg(progressPercent());
        }
        return QStringLiteral("Descargando %1…").arg(ver);
    }
    if (st == update::UpdateState::Available) {
        return QStringLiteral("Nueva versión %1").arg(ver);
    }
    return {};
}

QString UpdateView::pillTooltip() const {
    const QString ver = targetVersion();
    const auto st = effectiveState();
    if (st == update::UpdateState::Ready) {
        return QStringLiteral("Reiniciar y actualizar a %1").arg(ver);
    }
    if (st == update::UpdateState::Verifying) {
        return QStringLiteral("Verificando actualización %1").arg(ver);
    }
    if (st == update::UpdateState::Downloading) {
        const qint64 done = std::max<qint64>(0, effectiveDownloadedBytes());
        const qint64 total = effectiveTotalBytes();
        const qint64 speed = effectiveSpeedBytesPerSec();
        QStringList parts;
        if (total > 0) {
            parts.append(display::bytes(done) + QStringLiteral(" de ") + display::bytes(total));
        }
        if (speed > 0) {
            parts.append(display::speed(speed));
        }
        return parts.isEmpty() ? QStringLiteral("Ver progreso en Ajustes")
                               : parts.join(QStringLiteral(" · "));
    }
    if (st == update::UpdateState::Available) {
        if (manualOnly()) {
            return QStringLiteral("Ver actualización %1 en Ajustes").arg(ver);
        }
        if (majorUpdate()) {
            return QStringLiteral("Ver novedades de %1 en Ajustes").arg(ver);
        }
        const QString sz = sizeText();
        return sz.isEmpty() ? QStringLiteral("Descargar actualización %1").arg(ver)
                            : QStringLiteral("Descargar actualización %1 (%2)").arg(ver, sz);
    }
    return {};
}

int UpdateView::activeDownloadsCount() const {
    if (preview_) {
        return preview_->activeDownloads;
    }
    return active_downloads_ ? std::max(0, active_downloads_()) : 0;
}

void UpdateView::checkNow() {
    if (preview_) {
        return;
    }
    service_.checkNow(true);
}

void UpdateView::downloadNow() {
    if (preview_) {
        return;
    }
    service_.downloadNow();
}

void UpdateView::cancelDownload() {
    if (preview_) {
        return;
    }
    service_.cancelDownload();
}

void UpdateView::requestApply() {
    if (!ready()) {
        return;
    }
    if (activeDownloadsCount() > 0) {
        emit restartConfirmationRequested();
        return;
    }
    applyAndRestart();
}

void UpdateView::applyAndRestart() {
    if (preview_) {
        return;
    }
    service_.applyAndRestart();
}

void UpdateView::openManualDownload() {
    const QUrl url = manualDownloadUrl();
    if (url.isValid() && open_url_) {
        open_url_(url);
    }
}

void UpdateView::handlePillClick() {
    const auto st = effectiveState();
    if (st == update::UpdateState::Ready) {
        requestApply();
        return;
    }
    if (st == update::UpdateState::Downloading || st == update::UpdateState::Verifying) {
        emit openAboutRequested();
        return;
    }
    if (st == update::UpdateState::Available) {
        if (manualOnly() || majorUpdate()) {
            emit openAboutRequested();
        } else {
            downloadNow();
        }
    }
}

void UpdateView::requestRestartConfirmation() {
    emit restartConfirmationRequested();
}

void UpdateView::notifyActiveDownloadsChanged() {
    emit changed();
}

void UpdateView::previewState(const QString& state_name, const QString& mode_name) {
    if (state_name.isEmpty()) {
        preview_.reset();
        emit changed();
        return;
    }
    Preview p;
    p.targetVersion = QStringLiteral("0.5.0");
    p.notes = QStringLiteral(
        "• Actualización interna automática y manual con respaldo antes de aplicar.\n"
        "• Instalador de Windows con detección de versión, protección de mpv y modo oscuro.\n"
        "• Sincronización del inicio automático con el Administrador de tareas de Windows.");
    p.releaseUrl = QUrl(QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/"
                                       "tag/v0.5.0"));
    p.manualDownloadUrl = p.releaseUrl;
    p.lastCheckedAtMs = QDateTime(QDate::currentDate(), QTime(12, 30)).toMSecsSinceEpoch();

    if (state_name == u"uptodate") {
        p.state = update::UpdateState::Idle;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
    } else if (state_name == u"available") {
        p.state = update::UpdateState::Available;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
        p.totalBytes = 1'400'000;
    } else if (state_name == u"downloading") {
        p.state = update::UpdateState::Downloading;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
        p.downloadedBytes = 1'100'000;
        p.totalBytes = 1'400'000;
        p.speedBytesPerSec = 3'800'000;
    } else if (state_name == u"ready") {
        p.state = update::UpdateState::Ready;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
        p.downloadedBytes = 1'400'000;
        p.totalBytes = 1'400'000;
    } else if (state_name == u"error") {
        p.state = update::UpdateState::Error;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
        p.errorMessage = QStringLiteral("Sin conexión");
    } else if (state_name == u"notify-available") {
        p.state = update::UpdateState::Available;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::NotifyOnly);
        p.totalBytes = 1'400'000;
    } else if (state_name == u"notify-downloading") {
        p.state = update::UpdateState::Downloading;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::NotifyOnly);
        p.downloadedBytes = 1'100'000;
        p.totalBytes = 1'400'000;
        p.speedBytesPerSec = 3'800'000;
    } else if (state_name == u"confirm-restart") {
        p.state = update::UpdateState::Ready;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
        p.downloadedBytes = 1'400'000;
        p.totalBytes = 1'400'000;
        p.activeDownloads = 2;
    } else {
        p.state = update::UpdateState::Idle;
        p.mode = parsePreviewMode(mode_name, AppSettings::UpdateMode::Auto);
    }

    preview_ = std::move(p);
    emit changed();
}

} // namespace animehub::app
