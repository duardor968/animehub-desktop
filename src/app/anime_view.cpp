#include "anime_view.h"

#include "display.h"
#include "shell.h"
#include "util/file_names.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace animehub::app {

using downloads::TaskState;
using library::WatchState;

namespace {

// Downloads of this many episodes ask first.
constexpr int kConfirmFrom = 25;

const QLocale& spanish() {
    static const QLocale kSpanish(QLocale::Spanish, QLocale::Spain);
    return kSpanish;
}

// "9,1", "8,73", "10": up to `decimals`, none trailing.
QString decimal(double value, int decimals) {
    auto text = spanish().toString(value, 'f', decimals);
    if (text.contains(spanish().decimalPoint())) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(spanish().decimalPoint())) {
            text.chop(1);
        }
    }
    return text;
}

// "640", "1,2 mil", "48 mil", "1,6 M".
QString votes(int count) {
    if (count < 1000) {
        return QString::number(count);
    }
    if (count < 10'000) {
        return decimal(count / 1000.0, 1) + QStringLiteral(" mil");
    }
    if (count < 999'500) {
        return QString::number(std::lround(count / 1000.0)) + QStringLiteral(" mil");
    }
    return decimal(count / 1e6, 1) + QStringLiteral(" M");
}

QString relationLabel(api::RelationKind kind) {
    using api::RelationKind;
    switch (kind) {
    case RelationKind::Prequel:
        return QStringLiteral("Precuela");
    case RelationKind::Sequel:
        return QStringLiteral("Secuela");
    case RelationKind::MainStory:
        return QStringLiteral("Historia principal");
    case RelationKind::FullStory:
        return QStringLiteral("Historia completa");
    case RelationKind::SideStory:
        return QStringLiteral("Historia paralela");
    case RelationKind::SpinOff:
        return QStringLiteral("Spin-off");
    case RelationKind::Summary:
        return QStringLiteral("Resumen");
    case RelationKind::Alternative:
        return QStringLiteral("Versión alternativa");
    case RelationKind::AlternativeSetting:
        return QStringLiteral("Ambientación alternativa");
    case RelationKind::Other:
        break;
    }
    return QStringLiteral("Relacionado");
}

// "hoy 22:00", "mañana 22:00", "jueves 22:00" within a week, "12 de noviembre, 22:00" later.
QString airingText(const QDateTime& at) {
    const auto now = QDateTime::currentDateTime();
    if (!at.isValid() || at <= now) {
        return {};
    }
    const auto local = at.toLocalTime();
    const auto time = spanish().toString(local.time(), QStringLiteral("H:mm"));
    const auto days = now.date().daysTo(local.date());
    if (days == 0) {
        return QStringLiteral("hoy ") + time;
    }
    if (days == 1) {
        return QStringLiteral("mañana ") + time;
    }
    if (days < 7) {
        return spanish().dayName(local.date().dayOfWeek()) + QLatin1Char(' ') + time;
    }
    return spanish().toString(local.date(), QStringLiteral("d 'de' MMMM")) + QStringLiteral(", ") +
           time;
}

// A typed position: complete and valid, still incomplete, or wrong (with what is wrong).
struct Clock {
    bool complete = false;
    QString problem;
    qint64 ms = 0;
};

Clock parseClock(const QString& text, qint64 duration_ms) {
    static const QRegularExpression kAllowed(QStringLiteral("^[0-9:]*$"));
    static const QRegularExpression kClock(QStringLiteral("^(?:(\\d+):)?(\\d{1,2}):(\\d{2})$"));
    const QString kFormat = QStringLiteral("Escribe el minuto como 12:34 o 1:02:03.");
    const auto clock = text.trimmed();
    if (!kAllowed.match(clock).hasMatch()) {
        return {.problem = kFormat};
    }
    const auto parts = clock.split(QLatin1Char(':'));
    if (parts.size() > 3 || parts.last().size() > 2) {
        return {.problem = kFormat};
    }
    if (parts.size() < 2 || parts.last().size() < 2) {
        return {}; // "12", "12:", "12:3": still being typed
    }
    const auto match = kClock.match(clock);
    if (!match.hasMatch()) {
        return {.problem = kFormat};
    }
    const bool hours = !match.captured(1).isEmpty();
    const qint64 minutes = match.captured(2).toLongLong();
    const qint64 seconds = match.captured(3).toLongLong();
    if (seconds >= 60) {
        return {.problem = QStringLiteral("Los segundos llegan hasta 59.")};
    }
    if (hours && minutes >= 60) {
        return {.problem = QStringLiteral("Los minutos llegan hasta 59.")};
    }
    const qint64 ms = ((match.captured(1).toLongLong() * 60 + minutes) * 60 + seconds) * 1000;
    if (duration_ms > 0 && ms >= duration_ms) {
        return {.problem = QStringLiteral("El episodio dura ") + display::clock(duration_ms) +
                           QLatin1Char('.')};
    }
    return {.complete = true, .ms = ms};
}

bool unfinished(const downloads::Task& task) {
    return task.state != TaskState::Completed && task.state != TaskState::Cancelled;
}

QString percent(double fraction) {
    return QString::number(static_cast<int>(std::floor(std::clamp(fraction, 0.0, 1.0) * 100))) +
           QStringLiteral(" %");
}

} // namespace

AnimeView::AnimeView(Services services, QObject* parent)
    : QObject(parent), services_(services), episodes_(this), related_(this) {
    // Downloads report progress several times a second: one repaint per event-loop turn.
    notify_timer_.setSingleShot(true);
    notify_timer_.setInterval(0);
    connect(&notify_timer_, &QTimer::timeout, this, &AnimeView::changed);

    connect(&services_.library, &library::LibraryService::animeChanged, this,
            [this](const QString& slug) {
                if (slug == slug_) {
                    readLibrary();
                }
            });
    connect(&services_.library, &library::LibraryService::refreshFailed, this,
            [this](const QString& slug, const QString&) {
                if (slug == slug_) {
                    loadFailed();
                }
            });
    const auto on_task = [this](qint64 id) {
        const auto task = services_.downloads.task(id);
        if (!task || task->slug != slug_) {
            return;
        }
        const auto known = std::ranges::find(tasks_, id, &downloads::Task::id);
        if (known == tasks_.end()) {
            tasks_.append(*task);
        } else {
            *known = *task;
        }
        episodes_.setTask(*task);
        update();
    };
    connect(&services_.downloads, &downloads::DownloadManager::taskAdded, this, on_task);
    connect(&services_.downloads, &downloads::DownloadManager::taskChanged, this, on_task);
    connect(&services_.downloads, &downloads::DownloadManager::taskRemoved, this, [this] {
        readTasks();
        update();
    });
    connect(&services_.settings, &AppSettings::changed, this, &AnimeView::update);
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
    detail_.reset();
    positioned_ = false;
    related_.setCards({});
    emit slugChanged();
    readTasks();
    // Straight to the new list: an empty one in between cancels the rows being built.
    readLibrary();
    reload();
    emit changed(); // at once: the page never shows the previous anime
}

void AnimeView::reload() {
    if (slug_.isEmpty()) {
        return;
    }
    problem_.clear();
    fetchDetail();
    update();
}

void AnimeView::fetchDetail() {
    // One request for both: the library keeps a snapshot and fetches the episodes; score,
    // trailer, next airing and relations stay here.
    const auto generation = generation_;
    fetching_ = true;
    services_.api.anime(
        slug_, this,
        [this, generation](const api::Result<api::AnimeDetail>& result) {
            if (generation != generation_) {
                return;
            }
            if (!result) {
                fetching_ = false;
                loadFailed();
                return;
            }
            const bool fresh = result->origin == api::Origin::Network;
            detail_ = result->value;
            auto relations = detail_->relations;
            std::ranges::stable_sort(relations, {}, &api::Relation::position);
            QList<Card> cards;
            for (const auto& relation : relations) {
                if (relation.anime.slug.isEmpty() || relation.anime.slug == slug_) {
                    continue;
                }
                cards.append({.slug = relation.anime.slug,
                              .title = relation.anime.title,
                              .posterUrl = relation.anime.posterUrl,
                              .caption = relationLabel(relation.kind)});
            }
            related_.setCards(std::move(cards));
            if (fresh) {
                fetching_ = false;
                services_.library.refresh(*detail_, false);
                rememberRelated();
            }
            update();
        },
        api::CachePolicy::CacheThenNetwork);
}

void AnimeView::loadFailed() {
    problem_ = episodes_.episodes().isEmpty()
                   ? QStringLiteral("No se pudo cargar este anime. Revisa la conexión.")
                   : QString();
    update();
}

void AnimeView::rememberRelated() {
    // A related anime opens with its poster and title at once, as one from the catalog does.
    for (const auto& relation : detail_->relations) {
        const auto known = services_.library.repository().anime(relation.anime.slug);
        if (known && !*known && !relation.anime.slug.isEmpty()) {
            report(services_.library.remember(library::snapshotOf(relation.anime)));
        }
    }
}

void AnimeView::readTasks() {
    tasks_.clear();
    for (const auto& task : services_.downloads.tasks()) {
        if (task.slug == slug_) {
            tasks_.append(task);
        }
    }
    episodes_.setTasks(tasks_);
}

void AnimeView::readLibrary() {
    auto entry = services_.library.repository().entry(slug_);
    if (entry && *entry) {
        record_ = **entry;
    }
    auto list = services_.library.repository().episodes(slug_);
    auto episodes = list ? std::move(*list) : QList<library::EpisodeRecord>();
    if (positioned_) {
        episodes_.setEpisodes(std::move(episodes));
    } else {
        // The first list of an anime opens on the page of the episode to watch next.
        positioned_ = !episodes.isEmpty();
        episodes_.load(std::move(episodes), record_ ? record_->resumeNumber : std::nullopt);
    }
    if (record_ && record_->inLibrary && record_->newEpisodes > 0) {
        report(services_.library.seen(slug_)); // the page shows them; no longer news
    }
    update();
}

void AnimeView::update() {
    summary_ = summarize();
    if (!notify_timer_.isActive()) {
        notify_timer_.start();
    }
}

AnimeView::Summary AnimeView::summarize() const {
    Summary summary;
    const auto& list = episodes_.episodes();
    for (const auto& episode : list) {
        const bool watched = episode.state == WatchState::Watched;
        summary.watched += watched ? 1 : 0;
        summary.watching = summary.watching || episode.state == WatchState::Watching;
        if (!episode.filePath.isEmpty()) {
            ++summary.onDisk;
            summary.diskBytes += episode.fileSize;
            summary.watchedOnDisk = summary.watchedOnDisk || watched;
        }
        if (episodes_.missing(episode)) {
            summary.missing.append(episode.number);
            if (!watched) {
                summary.unwatchedMissing.append(episode.number);
            }
        }
    }

    // The series' current downloads: the unfinished ones and those finished since the first of
    // them was queued.
    QDateTime since;
    for (const auto& task : tasks_) {
        if (unfinished(task) && (!since.isValid() || task.createdAt < since)) {
            since = task.createdAt;
        }
    }
    double transferred = 0;
    for (const auto& task : tasks_) {
        if (task.state == TaskState::Cancelled) {
            continue;
        }
        if (task.state == TaskState::Completed) {
            if (since.isValid() && task.createdAt >= since) {
                ++summary.batchDone;
                ++summary.batchTotal;
            }
            continue;
        }
        ++summary.batchTotal;
        summary.partialBytes += task.bytesDone;
        if (task.bytesTotal > 0) {
            transferred +=
                static_cast<double>(task.bytesDone) / static_cast<double>(task.bytesTotal);
        }
        if (task.state == TaskState::Failed) {
            ++summary.failed;
        } else if (task.state == TaskState::Paused) {
            summary.paused = true;
        } else {
            summary.running = true;
        }
    }
    summary.paused = summary.paused && !summary.running;
    if (summary.batchTotal > 0) {
        summary.batchProgress = (summary.batchDone + transferred) / summary.batchTotal;
    }

    if (!list.isEmpty()) {
        summary.target =
            record_ && record_->resumeNumber ? *record_->resumeNumber : list.first().number;
        const auto target = episodes_.episode(*summary.target);
        if (target && !target->filePath.isEmpty()) {
            summary.primary = PrimaryAction::Play;
        } else if (summary.running) {
            summary.primary = PrimaryAction::Downloading;
        } else if (summary.paused) {
            summary.primary = PrimaryAction::Paused;
        } else if (!summary.missing.isEmpty()) {
            summary.primary = PrimaryAction::Download;
        }
    }
    return summary;
}

const library::AnimeRecord& AnimeView::source() const {
    return record_ ? static_cast<const library::AnimeRecord&>(*record_) : hint_;
}

QString AnimeView::title() const {
    return source().title;
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

QString AnimeView::score() const {
    if (!detail_ || !detail_->score || *detail_->score <= 0) {
        return {};
    }
    auto text = decimal(*detail_->score, 2);
    if (detail_->votes && *detail_->votes > 0) {
        text += QStringLiteral(" (") + votes(*detail_->votes) + QLatin1Char(')');
    }
    return text;
}

QString AnimeView::facts() const {
    const auto& anime = source();
    QStringList parts;
    if (!anime.category.isEmpty()) {
        parts.append(anime.category);
    }
    if (anime.startYear) {
        parts.append(QString::number(*anime.startYear));
    }
    if (const auto status = display::status(anime.status); !status.isEmpty()) {
        parts.append(status);
    }
    const int count = std::max(anime.episodeCount.value_or(0), episodes_.total());
    if (count > 1) {
        parts.append(QStringLiteral("%1 episodios").arg(count));
    }
    return parts.join(QStringLiteral(" · "));
}

QString AnimeView::genres() const {
    return source().genres;
}

QString AnimeView::nextAiring() const {
    return detail_ && airing() ? airingText(detail_->nextEpisodeAt) : QString();
}

QUrl AnimeView::trailerUrl() const {
    return detail_ ? detail_->trailerUrl : QUrl();
}

bool AnimeView::single() const {
    const auto& anime = source();
    const int count = anime.episodeCount.value_or(0);
    if (count == 1) {
        return true;
    }
    return count == 0 && episodes_.total() == 1 && anime.status == api::AnimeStatus::Finished;
}

bool AnimeView::airing() const {
    const auto status = source().status;
    return status == api::AnimeStatus::Airing || status == api::AnimeStatus::Upcoming;
}

bool AnimeView::inLibrary() const {
    return record_ && record_->inLibrary;
}

bool AnimeView::favorite() const {
    return record_ && record_->favorite;
}

bool AnimeView::notify() const {
    return !record_ || record_->notify;
}

bool AnimeView::autoDownload() const {
    return record_ && record_->autoDownload;
}

bool AnimeView::refreshing() const {
    return fetching_ || services_.library.isRefreshing(slug_);
}

QString AnimeView::folderPath() const {
    return QDir(services_.settings.downloadDirectory()).filePath(util::safeFileName(title()));
}

QString AnimeView::downloadFolder() const {
    return QDir::toNativeSeparators(folderPath());
}

bool AnimeView::canSendToJDownloader() const {
    return services_.settings.jdownloader() != AppSettings::JDownloader::Off;
}

double AnimeView::firstNumber() const {
    return episodes_.episodes().isEmpty() ? 0 : episodes_.episodes().first().number;
}

double AnimeView::lastNumber() const {
    return episodes_.episodes().isEmpty() ? 0 : episodes_.episodes().last().number;
}

QString AnimeView::primaryText() const {
    const bool film = source().category.startsWith(QStringLiteral("Pel"), Qt::CaseInsensitive);
    const int position = std::min(summary_.batchDone + 1, summary_.batchTotal);
    switch (summary_.primary) {
    case PrimaryAction::Play: {
        const auto episode = episodes_.episode(*summary_.target);
        const bool resume =
            episode && episode->state == WatchState::Watching && episode->positionMs > 0;
        const auto number = display::episodeNumber(*summary_.target);
        if (single()) {
            return resume ? QStringLiteral("Continuar") : QStringLiteral("Ver");
        }
        return resume ? QStringLiteral("Continuar · Ep. ") + number
                      : QStringLiteral("Ver ep. ") + number;
    }
    case PrimaryAction::Download:
        if (single()) {
            return film ? QStringLiteral("Descargar película") : QStringLiteral("Descargar");
        }
        return QStringLiteral("Descargar todo (%1)").arg(summary_.missing.size());
    case PrimaryAction::Downloading:
        return single()
                   ? QStringLiteral("Descargando · ") + percent(summary_.batchProgress)
                   : QStringLiteral("Descargando %1 de %2").arg(position).arg(summary_.batchTotal);
    case PrimaryAction::Paused:
        return single()
                   ? QStringLiteral("En pausa · ") + percent(summary_.batchProgress)
                   : QStringLiteral("En pausa · %1 de %2").arg(position).arg(summary_.batchTotal);
    case PrimaryAction::None:
        break;
    }
    return {};
}

bool AnimeView::showProgress() const {
    if (!inLibrary()) {
        return false;
    }
    return summary_.watched > 0 || summary_.watching || summary_.onDisk > 0 || hasDownloads();
}

QString AnimeView::watchedText() const {
    if (single()) {
        const auto& list = episodes_.episodes();
        if (list.isEmpty()) {
            return {};
        }
        const auto& film = list.first();
        if (film.state == WatchState::Watched) {
            return QStringLiteral("Visto");
        }
        if (film.state == WatchState::Watching && film.positionMs > 0) {
            return film.durationMs > 0 ? display::clock(film.positionMs) + QStringLiteral(" de ") +
                                             display::clock(film.durationMs)
                                       : display::clock(film.positionMs);
        }
        return QStringLiteral("Sin ver");
    }
    return QStringLiteral("%1 de %2").arg(summary_.watched).arg(episodes_.total());
}

double AnimeView::watchedProgress() const {
    const auto& list = episodes_.episodes();
    if (list.isEmpty()) {
        return 0;
    }
    if (single()) {
        const auto& film = list.first();
        if (film.state == WatchState::Watched) {
            return 1;
        }
        return film.state == WatchState::Watching && film.durationMs > 0
                   ? static_cast<double>(film.positionMs) / static_cast<double>(film.durationMs)
                   : 0;
    }
    return static_cast<double>(summary_.watched) / static_cast<double>(list.size());
}

QString AnimeView::diskText() const {
    if (summary_.onDisk == 0) {
        return {};
    }
    if (single()) {
        return display::bytes(summary_.diskBytes);
    }
    return QStringLiteral("%1 de %2 · ").arg(summary_.onDisk).arg(episodes_.total()) +
           display::bytes(summary_.diskBytes);
}

QString AnimeView::downloadText() const {
    if (!summary_.running && !summary_.paused) {
        return {};
    }
    if (single()) {
        return percent(summary_.batchProgress);
    }
    return QStringLiteral("%1 de %2")
        .arg(std::min(summary_.batchDone + 1, summary_.batchTotal))
        .arg(summary_.batchTotal);
}

double AnimeView::nextNumber() const {
    return record_ && record_->resumeNumber ? *record_->resumeNumber : -1;
}

QString AnimeView::nextTitle() const {
    const auto episode = episodes_.episode(nextNumber());
    if (!episode) {
        return {};
    }
    const auto label = display::shortEpisode(episode->number);
    return episode->title.isEmpty() ? label : label + QStringLiteral(" · ") + episode->title;
}

QUrl AnimeView::nextImage() const {
    const auto episode = episodes_.episode(nextNumber());
    return episode ? episode->imageUrl : QUrl();
}

double AnimeView::nextProgress() const {
    const auto episode = episodes_.episode(nextNumber());
    if (!episode || episode->state != WatchState::Watching || episode->durationMs <= 0) {
        return -1;
    }
    return static_cast<double>(episode->positionMs) / static_cast<double>(episode->durationMs);
}

bool AnimeView::nextHasFile() const {
    const auto episode = episodes_.episode(nextNumber());
    return episode && !episode->filePath.isEmpty();
}

bool AnimeView::nextBusy() const {
    const auto task = episodes_.task(nextNumber());
    return !nextHasFile() && task && task->state != TaskState::Paused &&
           task->state != TaskState::Failed;
}

bool AnimeView::nextFailed() const {
    const auto task = episodes_.task(nextNumber());
    return !nextHasFile() && task && task->state == TaskState::Failed;
}

QString AnimeView::nextState() const {
    const auto episode = episodes_.episode(nextNumber());
    if (!episode) {
        return {};
    }
    if (!episode->filePath.isEmpty()) {
        if (episode->state == WatchState::Watching && episode->positionMs > 0) {
            return episode->durationMs > 0
                       ? display::clock(episode->positionMs) + QStringLiteral(" de ") +
                             display::clock(episode->durationMs)
                       : display::clock(episode->positionMs);
        }
        return QStringLiteral("En disco");
    }
    const auto task = episodes_.task(episode->number);
    if (!task) {
        return QStringLiteral("Sin descargar");
    }
    const double fraction = task->bytesTotal > 0 ? static_cast<double>(task->bytesDone) /
                                                       static_cast<double>(task->bytesTotal)
                                                 : 0;
    switch (task->state) {
    case TaskState::Downloading:
        return QStringLiteral("Descargando · ") + percent(fraction);
    case TaskState::Verifying:
        return QStringLiteral("Comprobando el archivo");
    case TaskState::Paused:
        return QStringLiteral("En pausa · ") + percent(fraction);
    case TaskState::Failed:
        return display::issue(task->issue);
    default:
        return QStringLiteral("En cola");
    }
}

void AnimeView::report(const storage::Stored<void>& result) {
    if (!result) {
        emit message(QStringLiteral("No se pudo guardar el cambio: ") + result.error().message,
                     true);
    }
}

void AnimeView::joinLibrary() {
    if (!inLibrary()) {
        setInLibrary(true);
    }
}

void AnimeView::primary() {
    switch (summary_.primary) {
    case PrimaryAction::Play:
        play(*summary_.target);
        break;
    case PrimaryAction::Download:
        downloadMissing();
        break;
    case PrimaryAction::Downloading:
        pauseSeries();
        break;
    case PrimaryAction::Paused:
        resumeSeries();
        break;
    case PrimaryAction::None:
        break;
    }
}

void AnimeView::openNext() {
    const double number = nextNumber();
    if (number < 0) {
        return;
    }
    if (nextHasFile()) {
        play(number);
    } else if (!nextBusy()) {
        download(number);
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
    if (value) {
        report(services_.library.add(*record_));
        return;
    }
    const bool auto_download = record_->autoDownload;
    const auto result = services_.library.remove(slug_);
    report(result);
    if (!result) {
        return;
    }
    undo_ = [this, slug = slug_, auto_download] {
        auto& repository = services_.library.repository();
        report(repository.setInLibrary(slug, true));
        if (auto_download) {
            report(repository.setFlag(slug, library::Flag::AutoDownload, true));
        }
        services_.library.announce(slug);
    };
    emit undoable(QStringLiteral("Quitado de la biblioteca"));
}

void AnimeView::play(double number) {
    const auto episode = episodes_.episode(number);
    if (!episode || episode->filePath.isEmpty()) {
        return;
    }
    joinLibrary();
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
    downloadEpisodes({number});
}

void AnimeView::downloadMissing() {
    request(summary_.missing);
}

void AnimeView::downloadUnwatched() {
    request(summary_.unwatchedMissing);
}

void AnimeView::downloadSelection() {
    QList<double> numbers;
    for (const auto number : episodes_.selection()) {
        if (const auto episode = episodes_.episode(number);
            episode && episodes_.missing(*episode)) {
            numbers.append(number);
        }
    }
    request(numbers, true);
}

void AnimeView::request(const QList<double>& numbers, bool selection) {
    if (numbers.isEmpty()) {
        return;
    }
    if (numbers.size() >= kConfirmFrom) {
        emit downloadConfirmationNeeded(
            numbers,
            QStringLiteral("¿Descargar %1 episodios de %2?").arg(numbers.size()).arg(title()),
            QStringLiteral("Se guardarán en %1.").arg(downloadFolder()), selection);
        return;
    }
    downloadEpisodes(numbers);
    if (selection) {
        episodes_.clearSelection();
    }
}

void AnimeView::downloadEpisodes(const QList<double>& numbers) {
    QList<double> wanted;
    for (const auto number : numbers) {
        if (const auto episode = episodes_.episode(number);
            episode && episode->filePath.isEmpty()) {
            wanted.append(number);
        }
    }
    if (wanted.isEmpty()) {
        return;
    }
    joinLibrary();
    const auto queued =
        services_.downloads.enqueueMany(slug_, wanted, title(), downloads::TaskOrigin::Manual);
    if (!queued) {
        emit message(queued.error(), true);
    }
}

int AnimeView::missingBetween(double from, double to) const {
    return static_cast<int>(std::ranges::count_if(
        summary_.missing, [from, to](double number) { return number >= from && number <= to; }));
}

void AnimeView::downloadRange(double from, double to) {
    QList<double> numbers;
    for (const auto number : summary_.missing) {
        if (number >= from && number <= to) {
            numbers.append(number);
        }
    }
    downloadEpisodes(numbers);
}

void AnimeView::pauseSeries() {
    services_.downloads.pauseSeries(slug_);
}

void AnimeView::resumeSeries() {
    services_.downloads.resumeSeries(slug_);
}

void AnimeView::retryFailed() {
    services_.downloads.retryFailed(slug_);
}

void AnimeView::cancelSeries(bool confirmed) {
    if (!confirmed && summary_.partialBytes > 0) {
        emit cancelConfirmationNeeded(-1,
                                      QStringLiteral("¿Cancelar la descarga de %1?").arg(title()),
                                      QStringLiteral("Se borrarán los %1 descargados.")
                                          .arg(display::bytes(summary_.partialBytes)));
        return;
    }
    services_.downloads.cancelSeries(slug_);
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

void AnimeView::cancelDownload(double number, bool confirmed) {
    const auto task = episodes_.task(number);
    if (!task) {
        return;
    }
    if (!confirmed && task->bytesDone > 0) {
        emit cancelConfirmationNeeded(
            number,
            QStringLiteral("¿Cancelar la descarga del episodio %1?")
                .arg(display::episodeNumber(number)),
            QStringLiteral("Se borrarán los %1 descargados.").arg(display::bytes(task->bytesDone)));
        return;
    }
    services_.downloads.cancel(task->id);
}

void AnimeView::retryDownload(double number) {
    if (const auto task = episodes_.task(number)) {
        services_.downloads.retry(task->id);
    }
}

QList<AnimeView::SavedState>
AnimeView::saveStates(const std::function<bool(const library::EpisodeRecord&)>& chosen) const {
    QList<SavedState> saved;
    for (const auto& episode : episodes_.episodes()) {
        if (chosen(episode)) {
            saved.append({.number = episode.number,
                          .state = episode.state,
                          .positionMs = episode.positionMs,
                          .durationMs = episode.durationMs});
        }
    }
    return saved;
}

void AnimeView::offerUndo(const QString& text, QList<SavedState> saved) {
    if (saved.isEmpty()) {
        return;
    }
    undo_ = [this, slug = slug_, saved = std::move(saved)] { restore(slug, saved); };
    emit undoable(text);
}

void AnimeView::restore(const QString& slug, const QList<SavedState>& saved) {
    // Only one episode is in progress at a time: it goes last, after the others are back.
    auto ordered = saved;
    std::ranges::stable_partition(
        ordered, [](const SavedState& state) { return state.state != WatchState::Watching; });
    auto& repository = services_.library.repository();
    for (const auto& state : ordered) {
        if (auto stored = repository.setWatchState(slug, state.number, state.state,
                                                   state.positionMs, state.durationMs);
            !stored) {
            report(stored);
            break;
        }
    }
    services_.library.announce(slug);
}

void AnimeView::undo() {
    if (auto undo = std::exchange(undo_, {})) {
        undo();
    }
}

void AnimeView::markWatched(double number) {
    joinLibrary();
    report(services_.library.markWatched(slug_, number));
}

void AnimeView::markUnwatched(double number) {
    report(services_.library.markUnwatched(slug_, number));
}

void AnimeView::markWatchedThrough(double number) {
    const auto& episodes = episodes_.episodes();
    if (episodes.isEmpty()) {
        return;
    }
    auto saved = saveStates([number](const library::EpisodeRecord& episode) {
        return episode.number <= number && episode.state != WatchState::Watched;
    });
    joinLibrary();
    const auto result = services_.library.markWatchedThrough(slug_, number);
    report(result);
    if (result) {
        offerUndo(QStringLiteral("Episodios %1–%2 marcados como vistos")
                      .arg(display::episodeNumber(episodes.first().number),
                           display::episodeNumber(number)),
                  std::move(saved));
    }
}

void AnimeView::markAllWatched() {
    const auto& episodes = episodes_.episodes();
    if (episodes.isEmpty()) {
        return;
    }
    auto saved = saveStates(
        [](const library::EpisodeRecord& episode) { return episode.state != WatchState::Watched; });
    joinLibrary();
    const auto result = services_.library.markWatchedThrough(slug_, episodes.last().number);
    report(result);
    if (result) {
        offerUndo(QStringLiteral("Todo marcado como visto"), std::move(saved));
    }
}

void AnimeView::markAllUnwatched() {
    auto saved = saveStates([](const library::EpisodeRecord& episode) {
        return episode.state != WatchState::Unwatched;
    });
    const auto result = services_.library.markAllUnwatched(slug_);
    report(result);
    if (result) {
        offerUndo(QStringLiteral("Todo marcado como no visto"), std::move(saved));
    }
}

void AnimeView::markSelection(bool watched) {
    const auto numbers = episodes_.selection();
    const auto target = watched ? WatchState::Watched : WatchState::Unwatched;
    auto saved = saveStates([&numbers, target](const library::EpisodeRecord& episode) {
        return episode.state != target && numbers.contains(episode.number);
    });
    episodes_.clearSelection();
    if (saved.isEmpty()) {
        return;
    }
    if (watched) {
        joinLibrary();
    }
    auto& repository = services_.library.repository();
    for (const auto& state : saved) {
        if (auto stored = repository.setWatchState(slug_, state.number, target); !stored) {
            report(stored);
            break;
        }
    }
    services_.library.announce(slug_);
    const auto count = saved.size();
    const auto text = count == 1
                          ? QStringLiteral("Episodio %1 marcado como %2")
                                .arg(display::episodeNumber(saved.first().number),
                                     watched ? QStringLiteral("visto") : QStringLiteral("no visto"))
                          : QStringLiteral("%1 episodios marcados como %2")
                                .arg(QString::number(count), watched ? QStringLiteral("vistos")
                                                                     : QStringLiteral("no vistos"));
    offerUndo(text, std::move(saved));
}

bool AnimeView::clockValid(double number, const QString& clock) const {
    const auto episode = episodes_.episode(number);
    return parseClock(clock, episode ? episode->durationMs : 0).complete;
}

QString AnimeView::clockProblem(double number, const QString& clock) const {
    const auto episode = episodes_.episode(number);
    return parseClock(clock, episode ? episode->durationMs : 0).problem;
}

void AnimeView::continueFrom(double number, const QString& clock) {
    const auto episode = episodes_.episode(number);
    const auto parsed = parseClock(clock, episode ? episode->durationMs : 0);
    if (!parsed.complete) {
        return; // the dialog does not let an invalid position through
    }
    joinLibrary();
    report(services_.library.markWatching(slug_, number, parsed.ms));
}

void AnimeView::trashFile(double number) {
    const auto trashed = services_.library.trashFiles(slug_, {number});
    if (!trashed) {
        emit message(QStringLiteral("No se pudo guardar el cambio: ") + trashed.error().message,
                     true);
    } else if (trashed->failed > 0) {
        emit message(QStringLiteral("No se pudo mover el archivo a la Papelera: está en uso."),
                     true);
    } else {
        offerRestore(display::episode(number) + QStringLiteral(" en la Papelera"), trashed->files);
    }
}

void AnimeView::trashWatched() {
    const auto trashed = services_.library.trashWatched(slug_);
    if (!trashed) {
        emit message(QStringLiteral("No se pudo guardar el cambio: ") + trashed.error().message,
                     true);
        return;
    }
    if (trashed->failed > 0) {
        emit message(trashed->failed == 1
                         ? QStringLiteral("Un archivo sigue en el disco: está en uso.")
                         : QStringLiteral("%1 archivos siguen en el disco: están en uso.")
                               .arg(trashed->failed),
                     true);
        return;
    }
    if (trashed->count > 0) {
        offerRestore((trashed->count == 1
                          ? QStringLiteral("1 episodio en la Papelera · ")
                          : QStringLiteral("%1 episodios en la Papelera · ").arg(trashed->count)) +
                         display::bytes(trashed->bytes),
                     trashed->files);
    }
}

// Files the bin said where it keeps can come back with "Deshacer".
void AnimeView::offerRestore(const QString& text, QList<library::TrashedFile> files) {
    files.removeIf([](const library::TrashedFile& file) { return file.trashPath.isEmpty(); });
    if (files.isEmpty()) {
        emit message(text, false);
        return;
    }
    undo_ = [this, slug = slug_, files = std::move(files)] {
        const auto restored = services_.library.restore(slug, files);
        if (!restored) {
            emit message(
                QStringLiteral("No se pudo guardar el cambio: ") + restored.error().message, true);
        } else if (const auto missing = files.size() - *restored; missing > 0) {
            emit message(
                missing == 1
                    ? QStringLiteral("Un archivo ya no está en la Papelera.")
                    : QStringLiteral("%1 archivos ya no están en la Papelera.").arg(missing),
                true);
        }
    };
    emit undoable(text);
}

void AnimeView::showFile(double number) {
    const auto episode = episodes_.episode(number);
    if (episode && !episode->filePath.isEmpty()) {
        shell::reveal(episode->filePath);
    }
}

bool AnimeView::hasFolder() const {
    return !slug_.isEmpty() && QDir(folderPath()).exists();
}

void AnimeView::openFolder() {
    if (hasFolder()) {
        shell::open(folderPath());
    }
}

void AnimeView::openTrailer() {
    if (const auto url = trailerUrl(); url.isValid()) {
        QDesktopServices::openUrl(url);
    }
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
            services_.jdownloader.send(
                package, links, this,
                [this](bool sent, const QString& text) { emit message(text, !sent); });
        });
}

void AnimeView::setFavorite(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::Favorite, value));
}

void AnimeView::setNotify(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::Notify, value));
}

void AnimeView::setAutoDownload(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::AutoDownload, value));
}

} // namespace animehub::app
