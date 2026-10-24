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

// The locale's abbreviations without their full stop: "jue", "oct".
QString abbreviation(QString name) {
    if (name.endsWith(QLatin1Char('.'))) {
        name.chop(1);
    }
    return name;
}

// "hoy", "mañana" or, from then on, "jue" within the week and "jue 30 oct" with `dated`.
QString day(const QDate& date, const QDate& today, bool dated) {
    const auto days = today.daysTo(date);
    if (days == 0) {
        return QStringLiteral("hoy");
    }
    if (days == 1) {
        return QStringLiteral("mañana");
    }
    auto text = abbreviation(spanish().dayName(date.dayOfWeek(), QLocale::ShortFormat));
    if (dated || days >= 7) {
        text += QLatin1Char(' ') + QString::number(date.day()) + QLatin1Char(' ') +
                abbreviation(spanish().monthName(date.month(), QLocale::ShortFormat));
    }
    return text;
}

// "vie 13:34" within the week, "jue 6 nov, 13:34" past it; always dated with `dated`.
QString when(const QDateTime& at, bool dated) {
    const auto local = at.toLocalTime();
    const auto name = day(local.date(), QDate::currentDate(), dated);
    const auto time = local.toString(QStringLiteral("HH:mm"));
    return name + (name.contains(QLatin1Char(' ')) ? QStringLiteral(", ") : QStringLiteral(" ")) +
           time;
}

QVariantMap figure(const QString& caption, const QString& value, double progress = -1,
                   bool problem = false) {
    return {{QStringLiteral("caption"), caption},
            {QStringLiteral("value"), value},
            {QStringLiteral("progress"), progress},
            {QStringLiteral("problem"), problem}};
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
        if (!task) {
            return;
        }
        if (task->slug != slug_) {
            // Another series moving in the queue moves the next episode's place in it.
            if (summary_.place > 0) {
                update();
            }
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
    next_airing_.reset();
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
    fetchSchedule();
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

void AnimeView::fetchSchedule() {
    const auto generation = generation_;
    services_.api.schedule(
        this,
        [this, generation](const api::Result<QList<api::ScheduleEntry>>& result) {
            if (generation != generation_ || !result) {
                return;
            }
            const auto& entries = result->value;
            const auto slot = std::ranges::find(
                entries, slug_, [](const api::ScheduleEntry& entry) { return entry.anime.slug; });
            auto next = slot == entries.end()
                            ? std::nullopt
                            : schedule::next(*slot, QDateTime::currentDateTimeUtc());
            // A copy from the cache cannot tell a late episode from one already out.
            if (next && next->late && result->origin == api::Origin::Cache) {
                next.reset();
            }
            next_airing_ = next;
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

    // The series' downloads as the Downloads page counts them: every one still listed.
    double transferred = 0;
    bool moving = false;
    for (const auto& task : tasks_) {
        if (task.state == TaskState::Cancelled) {
            continue;
        }
        ++summary.batchTotal;
        if (task.state == TaskState::Completed) {
            ++summary.batchDone;
            continue;
        }
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
            if (task.state != TaskState::Waiting) {
                moving = true;
            } else if (!summary.retryAt.isValid() || task.retryAt < summary.retryAt) {
                summary.retryAt = task.retryAt;
            }
        }
    }
    summary.paused = summary.paused && !summary.running;
    if (moving) {
        summary.retryAt = {};
    }
    if (summary.batchTotal > 0) {
        summary.batchProgress = (summary.batchDone + transferred) / summary.batchTotal;
    }

    if (list.isEmpty()) {
        if (premiere().isValid()) {
            summary.primary = PrimaryAction::Upcoming;
        }
        return summary;
    }
    // The episode to watch next; with everything watched, none while it airs and the first
    // one to start again otherwise.
    const bool caught_up = summary.watched == list.size();
    if (record_ && record_->resumeNumber && episodes_.episode(*record_->resumeNumber)) {
        summary.target = *record_->resumeNumber;
    } else if (!caught_up) {
        summary.target = std::ranges::find_if(list, [](const library::EpisodeRecord& episode) {
                             return episode.state != WatchState::Watched;
                         })->number;
    } else if (!airing()) {
        summary.target = list.first().number;
    }
    if (!summary.target) {
        summary.primary = PrimaryAction::Upcoming;
        return summary;
    }
    const auto target = episodes_.episode(*summary.target);
    summary.job = episodes_.task(*summary.target);
    if (!target->filePath.isEmpty()) {
        summary.job.reset();
        summary.primary = caught_up ? PrimaryAction::Rewatch : PrimaryAction::Play;
    } else if (!summary.job) {
        summary.primary = PrimaryAction::Download;
    } else {
        switch (summary.job->state) {
        case TaskState::Queued: {
            // Its place among the tasks waiting for a slot, in the order they will start.
            int place = 0;
            for (const auto& task : services_.downloads.tasks()) {
                if (task.state == TaskState::Queued) {
                    ++place;
                    if (task.id == summary.job->id) {
                        summary.place = place;
                        break;
                    }
                }
            }
            summary.primary = PrimaryAction::Queued;
            break;
        }
        case TaskState::Waiting:
            summary.primary = PrimaryAction::Queued;
            break;
        case TaskState::Paused:
            summary.primary = PrimaryAction::Paused;
            break;
        case TaskState::Failed:
            summary.primary = PrimaryAction::Failed;
            break;
        default: // finding its file, transferring or checking it
            summary.primary = PrimaryAction::Downloading;
            break;
        }
    }
    return summary;
}

QDateTime AnimeView::premiere() const {
    if (!detail_ || !episodes_.episodes().isEmpty() ||
        detail_->nextEpisodeAt <= QDateTime::currentDateTime()) {
        return {};
    }
    return detail_->nextEpisodeAt;
}

const library::AnimeRecord& AnimeView::source() const {
    return record_ ? static_cast<const library::AnimeRecord&>(*record_) : hint_;
}

QString AnimeView::title() const {
    return source().title;
}

// Only when it says something else: "Kaguya-sama: Love is War" repeats "Kaguya-sama Love is War".
QString AnimeView::alternativeTitle() const {
    const auto alternative = record_ ? record_->alternativeTitle : QString();
    const auto letters = [](const QString& text) {
        QString kept;
        for (const auto c : text) {
            if (c.isLetterOrNumber()) {
                kept.append(c.toCaseFolded());
            }
        }
        return kept;
    };
    return letters(alternative) == letters(title()) ? QString() : alternative;
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
        parts.append(display::category(anime.category));
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

// Only while AnimeHub downloads: with JDownloader as the engine, "Descargar" already sends it
// there, followed and into the library's folder.
bool AnimeView::canSendToJDownloader() const {
    return services_.settings.jdownloader() != AppSettings::JDownloader::Off &&
           !services_.settings.preferredJDownloader();
}

double AnimeView::firstNumber() const {
    return episodes_.episodes().isEmpty() ? 0 : episodes_.episodes().first().number;
}

double AnimeView::lastNumber() const {
    return episodes_.episodes().isEmpty() ? 0 : episodes_.episodes().last().number;
}

// A verb and what it acts on: "Descargar ep. 9", or for a film "Reanudar la descarga".
QString AnimeView::primaryText() const {
    const auto object = [this](const QString& film) {
        return single() ? film : QStringLiteral(" ep. ") + display::episodeNumber(*summary_.target);
    };
    const auto download = QStringLiteral(" la descarga");
    switch (summary_.primary) {
    case PrimaryAction::Play: {
        const auto episode = episodes_.episode(*summary_.target);
        const bool resume =
            episode && episode->state == WatchState::Watching && episode->positionMs > 0;
        return (resume ? QStringLiteral("Continuar") : QStringLiteral("Ver")) + object({});
    }
    case PrimaryAction::Rewatch:
        return QStringLiteral("Ver de nuevo");
    case PrimaryAction::Download:
        return QStringLiteral("Descargar") +
               object(source().category.startsWith(QStringLiteral("Pel"), Qt::CaseInsensitive)
                          ? QStringLiteral(" película")
                          : QString());
    case PrimaryAction::Queued:
        return QStringLiteral("Pausar") + object(download);
    case PrimaryAction::Downloading:
        switch (summary_.job->state) {
        case TaskState::Resolving: // no size yet
            return QStringLiteral("Pausar") + object(download);
        case TaskState::Verifying: // too late to pause
            return QStringLiteral("Comprobando · ") + percent(primaryProgress());
        default:
            return QStringLiteral("Pausar · ") + percent(primaryProgress());
        }
    case PrimaryAction::Paused:
        return QStringLiteral("Reanudar") + object(download);
    case PrimaryAction::Failed:
        return QStringLiteral("Reintentar") + object(download);
    case PrimaryAction::Upcoming:
        return upcomingText();
    case PrimaryAction::None:
        break;
    }
    return {};
}

bool AnimeView::primaryEnabled() const {
    switch (summary_.primary) {
    case PrimaryAction::None:
    case PrimaryAction::Upcoming:
        return false;
    case PrimaryAction::Downloading:
        return summary_.job->state != TaskState::Verifying;
    default:
        return true;
    }
}

QString AnimeView::primaryHint() const {
    if (!summary_.target || !primaryEnabled()) {
        return {};
    }
    const auto download = single() ? QStringLiteral("la descarga")
                                   : QStringLiteral("la descarga del episodio ") +
                                         display::episodeNumber(*summary_.target);
    switch (summary_.primary) {
    case PrimaryAction::Queued:
    case PrimaryAction::Downloading:
        return QStringLiteral("Pausar ") + download;
    case PrimaryAction::Paused:
        return QStringLiteral("Reanudar ") + download;
    case PrimaryAction::Failed:
        return QStringLiteral("Reintentar ") + download;
    default:
        return {};
    }
}

double AnimeView::primaryProgress() const {
    const auto& job = summary_.job;
    if (!job || job->bytesTotal <= 0) {
        return 0;
    }
    return std::clamp(static_cast<double>(job->bytesDone) / static_cast<double>(job->bytesTotal),
                      0.0, 1.0);
}

QString AnimeView::upcomingText() const {
    if (const auto start = premiere(); start.isValid()) {
        return QStringLiteral("Estreno: ") + when(start, true);
    }
    if (!next_airing_ || !airing()) {
        return QStringLiteral("Al día");
    }
    if (next_airing_->late) {
        return QStringLiteral("Próximo: retrasado");
    }
    if (QDateTime::currentDateTimeUtc() >= next_airing_->at) {
        return QStringLiteral("Próximo: ahora");
    }
    // As the bar's "Emisión" says it, and within the button's width.
    return QStringLiteral("Próximo: ") + when(next_airing_->at, false);
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

// "Descarga En cola (2.º) · Vistos 12/13 · En disco 12/13 · 2,3 GB · Emisión vie 13:34 · Ep. 5";
// what the main button already says stays out.
QVariantList AnimeView::figures() const {
    QVariantList figures;
    const auto& list = episodes_.episodes();
    // Where the next episode's download is, beside the button that acts on it.
    if (const auto& job = summary_.job) {
        QString state;
        switch (job->state) {
        case TaskState::Queued:
            state = summary_.place > 0 ? QStringLiteral("En cola (%1.º)").arg(summary_.place)
                                       : QStringLiteral("En cola");
            break;
        case TaskState::Waiting:
            state = display::retry(job->retryAt);
            break;
        case TaskState::Resolving:
            state = display::download(*job);
            break;
        case TaskState::Paused: // counted as the button counts it while it runs
            state = QStringLiteral("En pausa · ") + percent(primaryProgress());
            break;
        case TaskState::Failed: // why, as the row says it
            state = display::issue(job->issue);
            break;
        default: // transferring or checking: the button counts it
            break;
        }
        if (!state.isEmpty()) {
            figures.append(
                figure(QStringLiteral("Descarga"), state, -1, job->state == TaskState::Failed));
        }
    }
    if (single()) {
        if (!list.isEmpty() && list.first().state == WatchState::Watching &&
            list.first().positionMs > 0) {
            const auto& film = list.first();
            figures.append(figure(QStringLiteral("Visionado"),
                                  film.durationMs > 0
                                      ? display::clock(film.positionMs) + QStringLiteral(" de ") +
                                            display::clock(film.durationMs)
                                      : display::clock(film.positionMs),
                                  watchedProgress()));
        }
    } else if (started()) {
        figures.append(figure(QStringLiteral("Vistos"),
                              QStringLiteral("%1/%2").arg(summary_.watched).arg(list.size()),
                              watchedProgress()));
    }
    if (summary_.onDisk > 0) {
        const auto size = display::bytes(summary_.diskBytes);
        // Counted as "Vistos" is: "12/13 · 2,3 GB".
        figures.append(figure(
            QStringLiteral("En disco"),
            single() ? size
                     : QStringLiteral("%1/%2 · ").arg(summary_.onDisk).arg(list.size()) + size));
    }
    // The series' other downloads: the main button follows the next episode's.
    const auto& job = summary_.job;
    const bool own_failed = job && job->state == TaskState::Failed;
    const int others =
        summary_.batchTotal - summary_.batchDone - summary_.failed - (job && !own_failed ? 1 : 0);
    if (others > 0) {
        figures.append(
            figure(summary_.paused              ? QStringLiteral("En pausa")
                   : summary_.retryAt.isValid() ? QStringLiteral("En espera")
                                                : QStringLiteral("Descargando"),
                   QStringLiteral("%1 de %2").arg(summary_.batchDone).arg(summary_.batchTotal),
                   summary_.batchProgress));
    }
    if (const int failed = summary_.failed - (own_failed ? 1 : 0); failed > 0) {
        figures.append(figure(QStringLiteral("Con error"),
                              failed == 1 ? QStringLiteral("1 episodio")
                                          : QStringLiteral("%1 episodios").arg(failed),
                              -1, true));
    }
    if (next_airing_ && airing() && summary_.primary != PrimaryAction::Upcoming) {
        const auto now = QDateTime::currentDateTimeUtc();
        const auto at = next_airing_->late        ? QStringLiteral("retrasado")
                        : now >= next_airing_->at ? QStringLiteral("ahora")
                                                  : when(next_airing_->at, false);
        figures.append(
            figure(QStringLiteral("Emisión"),
                   at + QStringLiteral(" · ") + display::shortEpisode(next_airing_->number)));
    }
    return figures;
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
    case PrimaryAction::Rewatch:
        play(*summary_.target);
        break;
    case PrimaryAction::Download:
        download(*summary_.target);
        break;
    case PrimaryAction::Queued:
    case PrimaryAction::Downloading:
        pauseDownload(*summary_.target);
        break;
    case PrimaryAction::Paused:
        resumeDownload(*summary_.target);
        break;
    case PrimaryAction::Failed:
        retryDownload(*summary_.target);
        break;
    case PrimaryAction::Upcoming:
    case PrimaryAction::None:
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
