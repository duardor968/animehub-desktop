#include "anime_view.h"

#include "display.h"
#include "shell.h"
#include "util/file_names.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace animehub::app {

using library::WatchState;

AnimeView::AnimeView(Services services, QObject* parent)
    : QObject(parent), services_(services), episodes_(this) {
    connect(&services_.library, &library::LibraryService::animeChanged, this,
            [this](const QString& slug) {
                if (slug == slug_) {
                    readLibrary();
                }
            });
    connect(&services_.library, &library::LibraryService::refreshFailed, this,
            [this](const QString& slug, const QString& detail) {
                if (slug != slug_) {
                    return;
                }
                problem_ = episodes_.episodes().isEmpty()
                               ? QStringLiteral("No se pudo cargar este anime. Revisa la conexión.")
                               : QString();
                Q_UNUSED(detail);
                emit changed();
            });
    const auto on_task = [this](qint64 id) {
        const auto task = services_.downloads.task(id);
        if (task && task->slug == slug_) {
            episodes_.setTask(*task);
            emit changed();
        }
    };
    connect(&services_.downloads, &downloads::DownloadManager::taskAdded, this, on_task);
    connect(&services_.downloads, &downloads::DownloadManager::taskChanged, this, on_task);
    connect(&services_.downloads, &downloads::DownloadManager::taskRemoved, this, [this] {
        QList<downloads::Task> mine;
        for (const auto& task : services_.downloads.tasks()) {
            if (task.slug == slug_) {
                mine.append(task);
            }
        }
        episodes_.setTasks(mine);
        emit changed();
    });
    connect(&services_.settings, &AppSettings::changed, this, &AnimeView::changed);
}

void AnimeView::open(const QString& slug, const library::AnimeRecord& hint) {
    if (slug == slug_ && record_) {
        reload();
        return;
    }
    ++generation_;
    slug_ = slug;
    hint_ = hint;
    problem_.clear();
    record_.reset();
    episodes_.setEpisodes({});
    emit slugChanged();
    readLibrary();
    reload();
}

void AnimeView::reload() {
    if (slug_.isEmpty()) {
        return;
    }
    problem_.clear();
    services_.library.refresh(slug_, false);
    emit changed();
}

void AnimeView::readLibrary() {
    auto entry = services_.library.repository().entry(slug_);
    if (entry && *entry) {
        record_ = **entry;
    }
    auto list = services_.library.repository().episodes(slug_);
    if (list) {
        episodes_.setEpisodes(std::move(*list));
    }
    QList<downloads::Task> mine;
    for (const auto& task : services_.downloads.tasks()) {
        if (task.slug == slug_) {
            mine.append(task);
        }
    }
    episodes_.setTasks(mine);
    if (record_ && record_->inLibrary && record_->newEpisodes > 0) {
        report(services_.library.seen(slug_)); // the page shows them; no longer news
    }
    emit changed();
}

QString AnimeView::title() const {
    return record_ ? record_->title : hint_.title;
}

QString AnimeView::alternativeTitle() const {
    const auto alternative = record_ ? record_->alternativeTitle : QString();
    return alternative.compare(title(), Qt::CaseInsensitive) == 0 ? QString() : alternative;
}

QString AnimeView::synopsis() const {
    return record_ && !record_->synopsis.isEmpty() ? record_->synopsis : hint_.synopsis;
}

QUrl AnimeView::posterUrl() const {
    return record_ && !record_->posterUrl.isEmpty() ? record_->posterUrl : hint_.posterUrl;
}

QUrl AnimeView::backdropUrl() const {
    return record_ && !record_->backdropUrl.isEmpty() ? record_->backdropUrl : hint_.backdropUrl;
}

QString AnimeView::facts() const {
    const auto& source = record_ ? static_cast<const library::AnimeRecord&>(*record_) : hint_;
    QStringList parts;
    if (!source.category.isEmpty()) {
        parts.append(source.category);
    }
    if (source.startYear) {
        parts.append(QString::number(*source.startYear));
    }
    if (const auto status = display::status(source.status); !status.isEmpty()) {
        parts.append(status);
    }
    const int count =
        std::max(source.episodeCount.value_or(0), static_cast<int>(episodes_.episodes().size()));
    if (count > 0) {
        parts.append(count == 1 ? QStringLiteral("1 episodio")
                                : QStringLiteral("%1 episodios").arg(count));
    }
    return parts.join(QStringLiteral(" · "));
}

QString AnimeView::genres() const {
    return record_ ? record_->genres : hint_.genres;
}

bool AnimeView::inLibrary() const {
    return record_ && record_->inLibrary;
}

bool AnimeView::favorite() const {
    return record_ && record_->favorite;
}

bool AnimeView::pending() const {
    return record_ && record_->pending;
}

bool AnimeView::notify() const {
    return !record_ || record_->notify;
}

bool AnimeView::autoDownload() const {
    return record_ && record_->autoDownload;
}

bool AnimeView::refreshing() const {
    return services_.library.isRefreshing(slug_);
}

QString AnimeView::folderPath() const {
    return QDir(services_.settings.downloadDirectory()).filePath(util::safeFileName(title()));
}

QString AnimeView::downloadFolder() const {
    return QDir::toNativeSeparators(folderPath());
}

bool AnimeView::canSendToJDownloader() const {
    return services_.settings.clickNLoad();
}

namespace {

// The episode the main button acts on: the one in progress, the next one, or the first.
std::optional<double> targetEpisode(const std::optional<library::LibraryEntry>& record,
                                    const QList<library::EpisodeRecord>& episodes) {
    if (episodes.isEmpty()) {
        return std::nullopt;
    }
    if (record && record->resumeNumber) {
        return record->resumeNumber;
    }
    return episodes.first().number;
}

} // namespace

AnimeView::PrimaryAction AnimeView::primaryAction() const {
    const auto number = targetEpisode(record_, episodes_.episodes());
    if (!number) {
        return PrimaryAction::None;
    }
    const auto episode = episodes_.episode(*number);
    if (episode && !episode->filePath.isEmpty()) {
        return PrimaryAction::Play;
    }
    const auto task = episodes_.task(*number);
    if (task && task->state != downloads::TaskState::Failed) {
        return PrimaryAction::Wait;
    }
    return PrimaryAction::Download;
}

QString AnimeView::primaryText() const {
    const auto number = targetEpisode(record_, episodes_.episodes());
    if (!number) {
        return {};
    }
    const auto label = display::episode(*number);
    switch (primaryAction()) {
    case PrimaryAction::Play: {
        const auto episode = episodes_.episode(*number);
        if (episode && episode->state == WatchState::Watching && episode->positionMs > 0) {
            return QStringLiteral("Continuar · ") + label;
        }
        return QStringLiteral("Ver ") + label.toLower();
    }
    case PrimaryAction::Wait:
        return QStringLiteral("Descargando ") + label.toLower();
    case PrimaryAction::Download:
        return QStringLiteral("Descargar ") + label.toLower();
    case PrimaryAction::None:
        break;
    }
    return {};
}

double AnimeView::resumeProgress() const {
    if (!record_ || !record_->resumeNumber || record_->resumePositionMs <= 0 ||
        record_->resumeDurationMs <= 0) {
        return -1;
    }
    return static_cast<double>(record_->resumePositionMs) /
           static_cast<double>(record_->resumeDurationMs);
}

QString AnimeView::resumeText() const {
    if (resumeProgress() < 0) {
        return {};
    }
    return display::clock(record_->resumePositionMs) + QStringLiteral(" / ") +
           display::clock(record_->resumeDurationMs);
}

void AnimeView::report(const storage::Stored<void>& result) {
    if (!result) {
        emit message(QStringLiteral("No se pudo guardar el cambio: ") + result.error().message,
                     true);
    }
}

void AnimeView::primary() {
    const auto number = targetEpisode(record_, episodes_.episodes());
    if (!number) {
        return;
    }
    switch (primaryAction()) {
    case PrimaryAction::Play:
        play(*number);
        break;
    case PrimaryAction::Download:
        download(*number);
        break;
    default:
        break;
    }
}

void AnimeView::setInLibrary(bool value) {
    if (!record_) {
        if (!value || hint_.slug.isEmpty()) {
            return;
        }
        report(services_.library.add(hint_));
        return;
    }
    report(value ? services_.library.add(*record_) : services_.library.remove(slug_));
}

void AnimeView::play(double number) {
    const auto episode = episodes_.episode(number);
    if (!episode || episode->filePath.isEmpty()) {
        return;
    }
    if (!inLibrary()) {
        setInLibrary(true);
    }
    const auto started = services_.player.play(
        {.slug = slug_,
         .number = number,
         .filePath = episode->filePath,
         .title = title() + QStringLiteral(" · ") + display::episode(number),
         .startMs = episode->state == WatchState::Watching ? episode->positionMs : 0});
    if (!started) {
        if (!QFile::exists(episode->filePath)) {
            report(services_.library.repository().clearEpisodeFile(slug_, number));
            readLibrary();
        }
        emit message(started.error(), true);
    }
}

void AnimeView::download(double number) {
    if (!inLibrary()) {
        setInLibrary(true);
    }
    const auto queued =
        services_.downloads.enqueue(slug_, number, title(), downloads::TaskOrigin::Manual);
    if (!queued) {
        emit message(queued.error(), true);
    }
}

void AnimeView::downloadUnwatched() {
    int queued = 0;
    for (const auto& episode : episodes_.episodes()) {
        if (episode.filePath.isEmpty() && episode.state != WatchState::Watched &&
            !episodes_.task(episode.number)) {
            download(episode.number);
            ++queued;
        }
    }
    if (queued > 0) {
        emit message(queued == 1
                         ? QStringLiteral("1 episodio en la cola de descargas.")
                         : QStringLiteral("%1 episodios en la cola de descargas.").arg(queued),
                     false);
    }
}

void AnimeView::pauseDownload(double number) {
    if (const auto task = episodes_.task(number)) {
        services_.downloads.pause(task->id);
    }
}

void AnimeView::resumeDownload(double number) {
    if (const auto task = episodes_.task(number)) {
        services_.downloads.resume(task->id);
    }
}

void AnimeView::cancelDownload(double number) {
    if (const auto task = episodes_.task(number)) {
        services_.downloads.cancel(task->id);
    }
}

void AnimeView::retryDownload(double number) {
    if (const auto task = episodes_.task(number)) {
        services_.downloads.retry(task->id);
    }
}

void AnimeView::markWatched(double number) {
    if (!inLibrary()) {
        setInLibrary(true);
    }
    report(services_.library.markWatched(slug_, number));
}

void AnimeView::markUnwatched(double number) {
    report(services_.library.markUnwatched(slug_, number));
}

void AnimeView::markWatchedThrough(double number) {
    if (!inLibrary()) {
        setInLibrary(true);
    }
    report(services_.library.markWatchedThrough(slug_, number));
}

void AnimeView::markAllWatched() {
    if (!episodes_.episodes().isEmpty()) {
        markWatchedThrough(episodes_.episodes().last().number);
    }
}

void AnimeView::markAllUnwatched() {
    report(services_.library.markAllUnwatched(slug_));
}

void AnimeView::continueFrom(double number, const QString& clock) {
    static const QRegularExpression kClock(QStringLiteral("^(?:(\\d+):)?(\\d{1,2}):(\\d{2})$"));
    const auto match = kClock.match(clock.trimmed());
    if (!match.hasMatch()) {
        emit message(QStringLiteral("Escribe el minuto como 12:34."), true);
        return;
    }
    const qint64 ms = ((match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 60 +
                       match.captured(3).toLongLong()) *
                      1000;
    if (!inLibrary()) {
        setInLibrary(true);
    }
    report(services_.library.markWatching(slug_, number, ms));
}

void AnimeView::trashFile(double number) {
    const auto episode = episodes_.episode(number);
    if (!episode || episode->filePath.isEmpty()) {
        return;
    }
    if (QFile::exists(episode->filePath) && !QFile::moveToTrash(episode->filePath)) {
        emit message(QStringLiteral("No se pudo mover el archivo a la papelera."), true);
        return;
    }
    report(services_.library.repository().clearEpisodeFile(slug_, number));
    readLibrary();
    emit message(QStringLiteral("El archivo está en la papelera."), false);
}

void AnimeView::showFile(double number) {
    const auto episode = episodes_.episode(number);
    if (episode && !episode->filePath.isEmpty()) {
        shell::reveal(episode->filePath);
    }
}

void AnimeView::openFolder() {
    const auto folder = folderPath();
    if (!QDir(folder).exists()) {
        emit message(QStringLiteral("Aún no hay descargas de este anime."), false);
        return;
    }
    shell::open(folder);
}

void AnimeView::sendToJDownloader(double number) {
    const auto generation = generation_;
    const auto package = title();
    services_.api.resolve(
        slug_,
        {.episodeNumbers = {number},
         .audio = services_.settings.dubbed() ? api::Audio::Dub : api::Audio::Sub,
         .providers = {api::Provider::Mega, api::Provider::Pixeldrain, api::Provider::Mp4Upload,
                       api::Provider::OneFichier}},
        this, [this, generation, package](const api::Result<api::Resolution>& result) {
            if (generation != generation_) {
                return;
            }
            QList<QUrl> links;
            if (result) {
                for (const auto& episode : result->value.episodes) {
                    for (const auto& link : episode.links) {
                        links.append(link.url);
                    }
                }
            }
            if (links.isEmpty()) {
                emit message(QStringLiteral("No hay enlaces para este episodio."), true);
                return;
            }
            services_.clickNLoad.send(
                package, links, this,
                [this](const std::expected<void, downloads::ClickNLoad::Failure>& sent) {
                    if (sent) {
                        emit message(QStringLiteral("Enlaces enviados a JDownloader."), false);
                        return;
                    }
                    switch (sent.error()) {
                    case downloads::ClickNLoad::Failure::Unreachable:
                        emit message(QStringLiteral("Abre JDownloader con Click'n'Load activo "
                                                    "(puerto 9666)."),
                                     true);
                        break;
                    case downloads::ClickNLoad::Failure::Foreign:
                        emit message(QStringLiteral("Otro programa está usando el puerto 9666."),
                                     true);
                        break;
                    case downloads::ClickNLoad::Failure::Rejected:
                        emit message(QStringLiteral("JDownloader no aceptó los enlaces."), true);
                        break;
                    }
                });
        });
}

void AnimeView::setFavorite(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::Favorite, value));
}

void AnimeView::setPending(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::Pending, value));
}

void AnimeView::setNotify(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::Notify, value));
}

void AnimeView::setAutoDownload(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::AutoDownload, value));
}

} // namespace animehub::app
