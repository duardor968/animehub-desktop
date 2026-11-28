#include "anime_view.h"

#include "display.h"
#include "shell.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

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

// "1", "842", "1.842", "917.992": Spanish dot-grouped integer for exact vote counts.
QString groupedInteger(int count) {
    const auto digits = QString::number(std::max(count, 0));
    QString out;
    out.reserve(digits.size() + digits.size() / 3);
    const auto len = digits.size();
    for (qsizetype i = 0; i < len; ++i) {
        if (i > 0 && (len - i) % 3 == 0) {
            out.append(QLatin1Char('.'));
        }
        out.append(digits[i]);
    }
    return out;
}

// Offline fallback slug for a stored Spanish genre label: "Fantasía" -> "fantasia",
// "Ciencia Ficción" -> "ciencia-ficcion".
QString slugifyGenre(const QString& text) {
    const auto decomposed = text.normalized(QString::NormalizationForm_D).toLower();
    QString out;
    out.reserve(decomposed.size());
    bool pending_dash = false;
    for (const auto ch : decomposed) {
        if (ch.category() == QChar::Mark_NonSpacing) {
            continue;
        }
        if ((ch >= u'a' && ch <= u'z') || (ch >= u'0' && ch <= u'9')) {
            if (pending_dash && !out.isEmpty()) {
                out.append(QLatin1Char('-'));
            }
            pending_dash = false;
            out.append(ch);
        } else {
            pending_dash = true;
        }
    }
    return out;
}

QString shortMonth(int month) {
    static constexpr std::array<std::u16string_view, 12> kMonths{
        u"Ene", u"Feb", u"Mar", u"Abr", u"May", u"Jun",
        u"Jul", u"Ago", u"Sep", u"Oct", u"Nov", u"Dic",
    };
    return month >= 1 && month <= 12
               ? QStringView(kMonths[static_cast<size_t>(month - 1)]).toString()
               : QString();
}

// Smart Spanish emission range: "Oct – Dic 2026", "Sep 2023 – Mar 2024", "Oct 2026", or "2026".
QString formatEmission(const QDate& start, const QDate& end, std::optional<int> fallback_year) {
    if (start.isValid() && end.isValid()) {
        if (start.year() == end.year()) {
            if (start.month() == end.month()) {
                return shortMonth(start.month()) + QLatin1Char(' ') + QString::number(start.year());
            }
            return shortMonth(start.month()) + QStringLiteral(" – ") + shortMonth(end.month()) +
                   QLatin1Char(' ') + QString::number(start.year());
        }
        return shortMonth(start.month()) + QLatin1Char(' ') + QString::number(start.year()) +
               QStringLiteral(" – ") + shortMonth(end.month()) + QLatin1Char(' ') +
               QString::number(end.year());
    }
    if (start.isValid()) {
        return shortMonth(start.month()) + QLatin1Char(' ') + QString::number(start.year());
    }
    if (end.isValid()) {
        return shortMonth(end.month()) + QLatin1Char(' ') + QString::number(end.year());
    }
    return fallback_year ? QString::number(*fallback_year) : QString();
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

QVariantMap figure(const QString& caption, const QString& value, double progress = -1,
                   bool problem = false) {
    return {{QStringLiteral("caption"), caption},
            {QStringLiteral("value"), value},
            {QStringLiteral("progress"), progress},
            {QStringLiteral("problem"), problem}};
}

// A download that only waits to try again can try now.
QVariantMap retryable(QVariantMap figure) {
    figure.insert(QStringLiteral("action"), QStringLiteral("Reintentar ahora"));
    return figure;
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
    : QObject(parent), services_(std::move(services)), episodes_(this), related_(this) {
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
    // The library asks for the detail and the episodes at once and keeps a snapshot; score,
    // trailer, next airing and relations stay here.
    const auto generation = generation_;
    fetching_ = true;
    services_.library.refreshShown(
        slug_, this, [this, generation](const api::Result<api::AnimeDetail>& result) {
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
                rememberRelated();
            }
            update();
        });
}

void AnimeView::fetchSchedule() {
    const auto generation = generation_;
    services_.api.schedule(
        this,
        [this, generation](const api::Result<api::Schedule>& result) {
            if (generation != generation_ || !result) {
                return;
            }
            const auto& entries = result->value.entries;
            const auto slot = std::ranges::find(
                entries, slug_, [](const api::ScheduleEntry& entry) { return entry.anime.slug; });
            auto next = slot == entries.end()
                            ? std::nullopt
                            : schedule::next(*slot, QDateTime::currentDateTimeUtc());
            // A cached or server-stale schedule cannot tell a late episode from one already out.
            if (next && next->late &&
                (result->origin == api::Origin::Cache || result->value.stale)) {
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
                summary.transferring = summary.transferring || task.state != TaskState::Queued;
            } else if (!summary.retryAt.isValid() || task.retryAt < summary.retryAt) {
                summary.retryAt = task.retryAt;
            }
        }
    }
    summary.paused = summary.paused && !summary.running;
    summary.active = summary.batchTotal - summary.batchDone - summary.failed;
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

    const auto queued_place = [this](qint64 task_id) {
        int place = 0;
        for (const auto& task : services_.downloads.tasks()) {
            if (task.state == TaskState::Queued) {
                ++place;
                if (task.id == task_id) {
                    return place;
                }
            }
        }
        return 0;
    };

    // Single-episode work or film: follow its one file or download directly.
    if (single()) {
        summary.target = list.first().number;
        const auto& film = list.first();
        summary.job = episodes_.task(film.number);
        if (!film.filePath.isEmpty()) {
            summary.job.reset();
            summary.primary = caught_up ? PrimaryAction::Rewatch : PrimaryAction::Play;
        } else if (!summary.job) {
            summary.primary = PrimaryAction::Download;
        } else {
            switch (summary.job->state) {
            case TaskState::Queued:
                summary.place = queued_place(summary.job->id);
                summary.primary = PrimaryAction::Queued;
                break;
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

    // Dual-Role series logic:
    // 1) Target episode (next unwatched, or ep. 1 to rewatch a finished series) is on disk.
    if (summary.target) {
        if (const auto target = episodes_.episode(*summary.target);
            target && !target->filePath.isEmpty()) {
            summary.primary = caught_up ? PrimaryAction::Rewatch : PrimaryAction::Play;
            return summary;
        }
    }

    // 2) Target is not on disk (or caught up on an airing series) and unqueued episodes remain:
    //    stay on global DownloadAll even while individual episodes are downloading or paused.
    if (!summary.missing.isEmpty()) {
        summary.primary = PrimaryAction::DownloadAll;
        return summary;
    }

    // 3) No unqueued episodes left: act on the series' active, paused, or failed downloads.
    const auto find_task = [this](const auto& matches) -> std::optional<downloads::Task> {
        for (const auto& task : tasks_) {
            if (matches(task.state)) {
                return task;
            }
        }
        return std::nullopt;
    };
    if (summary.running) {
        summary.primary = PrimaryAction::PauseAll;
        if (summary.active == 1) {
            summary.job = find_task([](TaskState state) {
                return state != TaskState::Cancelled && state != TaskState::Completed &&
                       state != TaskState::Failed;
            });
            if (summary.job && summary.job->state == TaskState::Queued) {
                summary.place = queued_place(summary.job->id);
            }
        }
        return summary;
    }
    if (summary.paused) {
        summary.primary = PrimaryAction::ResumeAll;
        if (summary.active == 1) {
            summary.job = find_task([](TaskState state) { return state == TaskState::Paused; });
        }
        return summary;
    }
    if (summary.failed > 0) {
        summary.primary = PrimaryAction::Failed;
        if (summary.failed == 1) {
            summary.job = find_task([](TaskState state) { return state == TaskState::Failed; });
        }
        return summary;
    }

    // 4) Caught up with nothing missing or in the queue.
    if (airing()) {
        summary.primary = PrimaryAction::Upcoming;
    } else if (summary.onDisk > 0) {
        summary.target = list.first().number;
        summary.primary = PrimaryAction::Rewatch;
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
    const auto text = scoreText();
    if (text.isEmpty()) {
        return {};
    }
    if (detail_->votes && *detail_->votes > 0) {
        return text + QStringLiteral(" (") + votes(*detail_->votes) + QLatin1Char(')');
    }
    return text;
}

QString AnimeView::scoreText() const {
    if (!detail_ || !detail_->score || *detail_->score <= 0) {
        return {};
    }
    return decimal(*detail_->score, 2);
}

QString AnimeView::votesText() const {
    if (!detail_ || !detail_->votes || *detail_->votes <= 0) {
        return {};
    }
    const int count = *detail_->votes;
    return count == 1 ? QStringLiteral("1 voto") : groupedInteger(count) + QStringLiteral(" votos");
}

QString AnimeView::scoreSource() const {
    return QStringLiteral("AnimeAV1");
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

QVariantList AnimeView::genreChips() const {
    QVariantList result;
    if (detail_ && !detail_->genres.isEmpty()) {
        result.reserve(detail_->genres.size());
        for (const auto& genre : detail_->genres) {
            const auto name = genre.name.trimmed();
            if (name.isEmpty()) {
                continue;
            }
            const auto slug = genre.slug.isEmpty() ? slugifyGenre(name) : genre.slug;
            result.append(
                QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("slug"), slug}});
        }
        return result;
    }
    const auto raw = source().genres;
    if (raw.isEmpty()) {
        return result;
    }
    for (const auto& part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const auto name = part.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        result.append(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("slug"), slugifyGenre(name)}});
    }
    return result;
}

QString AnimeView::sourceUrl() const {
    return detail_ && detail_->sourceUrl.isValid() ? detail_->sourceUrl.toString() : QString();
}

bool AnimeView::mature() const {
    return detail_ && detail_->mature;
}

namespace {

QString tone(api::AnimeStatus status) {
    switch (status) {
    case api::AnimeStatus::Airing:
        return QStringLiteral("airing");
    case api::AnimeStatus::Finished:
        return QStringLiteral("finished");
    case api::AnimeStatus::Upcoming:
        return QStringLiteral("upcoming");
    case api::AnimeStatus::Paused:
        return QStringLiteral("paused");
    case api::AnimeStatus::Unknown:
        break;
    }
    return {};
}

QVariantMap chip(const QString& text, const QString& badge = {}) {
    return {{QStringLiteral("text"), text}, {QStringLiteral("tone"), badge}};
}

QVariantMap detail(const QString& label, const QString& value) {
    return {{QStringLiteral("label"), label},
            {QStringLiteral("name"), label},
            {QStringLiteral("value"), value}};
}

} // namespace

QString AnimeView::statusText() const {
    return display::status(source().status);
}

QString AnimeView::statusTone() const {
    return tone(source().status);
}

QString AnimeView::metaLine() const {
    const auto& anime = source();
    QStringList parts;
    if (!anime.category.isEmpty()) {
        parts.append(display::category(anime.category));
    }
    if (anime.startYear) {
        parts.append(QString::number(*anime.startYear));
    }
    const int count = std::max(anime.episodeCount.value_or(0), episodes_.total());
    if (count > 1) {
        parts.append(QStringLiteral("%1 episodios").arg(count));
    }
    return parts.join(QStringLiteral(" · "));
}

QVariantList AnimeView::chips() const {
    QVariantList chips;
    if (const auto status = statusText(); !status.isEmpty()) {
        chips.append(chip(status, statusTone()));
    }
    return chips;
}

QVariantList AnimeView::details() const {
    const auto& anime = source();
    QVariantList details;
    const auto category =
        !anime.category.isEmpty()
            ? anime.category
            : (detail_ && detail_->category ? detail_->category->name : QString());
    if (!category.isEmpty()) {
        details.append(detail(QStringLiteral("Tipo"), display::category(category)));
    }
    const auto emission = formatEmission(detail_ ? detail_->startDate : QDate(),
                                         detail_ ? detail_->endDate : QDate(), anime.startYear);
    if (!emission.isEmpty()) {
        details.append(detail(QStringLiteral("Emisión"), emission));
    }
    const auto effective_status = anime.status != api::AnimeStatus::Unknown
                                      ? anime.status
                                      : (detail_ ? detail_->status : api::AnimeStatus::Unknown);
    if (const auto status = display::status(effective_status); !status.isEmpty()) {
        details.append(detail(QStringLiteral("Estado"), status));
    }
    const int count =
        std::max({anime.episodeCount.value_or(0), detail_ ? detail_->episodeCount.value_or(0) : 0,
                  episodes_.total()});
    if (count > 1 || (count == 1 && !single())) {
        details.append(detail(QStringLiteral("Episodios"), QString::number(count)));
    }
    if (const auto scored = scoreText(); !scored.isEmpty()) {
        auto text = scored;
        if (const auto voted = votesText(); !voted.isEmpty()) {
            text += QStringLiteral(" (") + voted + QLatin1Char(')');
        }
        auto row = detail(QStringLiteral("Puntuación (AnimeAV1)"), text);
        row.insert(QStringLiteral("icon"), QStringLiteral("starFilled"));
        details.append(std::move(row));
    }
    return details;
}

bool AnimeView::hasDetails() const {
    return !details().isEmpty();
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

// Still gets new episodes: airing, about to, or between seasons.
bool AnimeView::airing() const {
    const auto status = source().status;
    return status == api::AnimeStatus::Airing || status == api::AnimeStatus::Upcoming ||
           status == api::AnimeStatus::Paused;
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

// The anime's own folder, where the queue puts its episodes.
QString AnimeView::folderPath() const {
    return services_.library.repository()
        .workFolder(services_.settings.downloadDirectory(), slug_, title())
        .value_or(QString());
}

QString AnimeView::downloadFolder() const {
    return QDir::toNativeSeparators(folderPath());
}

double AnimeView::firstNumber() const {
    return episodes_.episodes().isEmpty() ? 0 : episodes_.episodes().first().number;
}

double AnimeView::lastNumber() const {
    return episodes_.episodes().isEmpty() ? 0 : episodes_.episodes().last().number;
}

bool AnimeView::showSecondaryDownload() const {
    return !single() &&
           (summary_.primary == PrimaryAction::Play ||
            summary_.primary == PrimaryAction::Rewatch) &&
           !summary_.missing.isEmpty();
}

QString AnimeView::batchDownloadText() const {
    const int count = missingCount();
    if (count <= 0) {
        return {};
    }
    if (count == 1) {
        return QStringLiteral("Descargar ") + EpisodeModel::countText(summary_.missing);
    }
    if (hasDownloads() || summary_.onDisk > 0) {
        return QStringLiteral("Descargar restantes (%1)").arg(count);
    }
    return QStringLiteral("Descargar todo (%1)").arg(count);
}

// A verb and what it acts on: "Ver ep. 9", "Descargar todo (8)", or "Reanudar la descarga".
QString AnimeView::primaryText() const {
    const auto object = [this](const QString& film) {
        return single() || !summary_.target
                   ? film
                   : QStringLiteral(" ep. ") + display::episodeNumber(*summary_.target);
    };
    const auto download = QStringLiteral(" la descarga");
    switch (summary_.primary) {
    case PrimaryAction::Play: {
        const auto episode = summary_.target ? episodes_.episode(*summary_.target) : std::nullopt;
        const bool resume =
            episode && episode->state == WatchState::Watching && episode->positionMs > 0;
        return (resume ? QStringLiteral("Continuar") : QStringLiteral("Ver")) + object({});
    }
    case PrimaryAction::Rewatch:
        return QStringLiteral("Volver a ver");
    case PrimaryAction::Download:
        return QStringLiteral("Descargar") +
               object(source().category.startsWith(QStringLiteral("Pel"), Qt::CaseInsensitive)
                          ? QStringLiteral(" película")
                          : QString());
    case PrimaryAction::Queued:
        return QStringLiteral("Pausar") + object(download);
    case PrimaryAction::Downloading:
        if (!summary_.job) {
            return QStringLiteral("Pausar") + object(download);
        }
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
        return single() ? QStringLiteral("Reintentar la descarga")
                        : QStringLiteral("Reintentar fallidos");
    case PrimaryAction::DownloadAll:
        return batchDownloadText();
    case PrimaryAction::PauseAll:
        if (summary_.active == 1 && summary_.job) {
            if (summary_.job->state == TaskState::Verifying) {
                return QStringLiteral("Comprobando · ") + percent(primaryProgress());
            }
            if (summary_.job->state == TaskState::Downloading) {
                return QStringLiteral("Pausar · ") + percent(primaryProgress());
            }
        }
        return QStringLiteral("Pausar descargas");
    case PrimaryAction::ResumeAll:
        return QStringLiteral("Reanudar descargas");
    case PrimaryAction::Upcoming:
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
        return summary_.job && summary_.job->state != TaskState::Verifying;
    case PrimaryAction::PauseAll:
        return !(summary_.active == 1 && summary_.job &&
                 summary_.job->state == TaskState::Verifying);
    default:
        return true;
    }
}

QString AnimeView::primaryHint() const {
    if (!primaryEnabled()) {
        return {};
    }
    if (batch()) {
        const auto verb = summary_.primary == PrimaryAction::PauseAll ? QStringLiteral("Pausar")
                                                                      : QStringLiteral("Reanudar");
        if (summary_.active == 1 && summary_.job) {
            return verb + QStringLiteral(" la descarga del episodio ") +
                   display::episodeNumber(summary_.job->number);
        }
        return verb + QStringLiteral(" la descarga de %1 episodios").arg(summary_.active);
    }
    if (summary_.primary == PrimaryAction::Failed && !single()) {
        if (summary_.failed == 1 && summary_.job) {
            return QStringLiteral("Reintentar la descarga del episodio ") +
                   display::episodeNumber(summary_.job->number);
        }
        return summary_.failed == 1
                   ? QStringLiteral("Reintentar la descarga fallida")
                   : QStringLiteral("Reintentar %1 descargas fallidas").arg(summary_.failed);
    }
    if (!summary_.target) {
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

bool AnimeView::batch() const {
    return summary_.primary == PrimaryAction::PauseAll ||
           summary_.primary == PrimaryAction::ResumeAll;
}

double AnimeView::primaryProgress() const {
    if (summary_.job &&
        (summary_.primary == PrimaryAction::Downloading ||
         summary_.primary == PrimaryAction::Paused || (batch() && summary_.active == 1))) {
        return summary_.job->bytesTotal > 0
                   ? std::clamp(static_cast<double>(summary_.job->bytesDone) /
                                    static_cast<double>(summary_.job->bytesTotal),
                                0.0, 1.0)
                   : 0.0;
    }
    if (batch()) {
        return summary_.batchProgress;
    }
    return 0.0;
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

// "Descarga En cola (2.º) · Vistos 12/13 · En disco 12/13 · 2,3 GB · Próximo episodio Viernes
// 13:34 · Ep. 5"; what the main button already says stays out. Episodes are counted "x/y"
// throughout.
QVariantList AnimeView::figures() const {
    QVariantList figures;
    const auto& list = episodes_.episodes();
    const auto& job = summary_.job;
    const bool own_failed = job && job->state == TaskState::Failed;
    // The next episode's download alone, or the series' downloads together.
    const bool alone = job && summary_.active - (own_failed ? 0 : 1) == 0;
    if (alone) {
        QString state;
        switch (job->state) {
        case TaskState::Queued:
            state = summary_.place > 0 ? QStringLiteral("En cola (%1.º)").arg(summary_.place)
                                       : QStringLiteral("En cola");
            break;
        case TaskState::Waiting: // why it waits and until when, as the Downloads row says it
        case TaskState::Resolving:
            state = display::download(*job);
            break;
        case TaskState::Paused: // counted as the button counts it while it runs
            state = QStringLiteral("En pausa · ") + percent(primaryProgress());
            break;
        case TaskState::Failed: // why, as the row says it
            state = display::issue(*job);
            break;
        default: // transferring or checking: the button counts it
            break;
        }
        if (!state.isEmpty()) {
            const auto shown = figure(QStringLiteral("Descarga"), state, -1, own_failed);
            figures.append(job->state == TaskState::Waiting ? retryable(shown) : shown);
        }
    } else if (summary_.active > 0) {
        const bool waiting =
            !summary_.paused && !summary_.transferring && summary_.retryAt.isValid();
        const auto state = summary_.paused         ? QStringLiteral("En pausa")
                           : summary_.transferring ? QStringLiteral("Descargando")
                           : waiting               ? QStringLiteral("En espera")
                                                   : QStringLiteral("En cola");
        QString text;
        if (summary_.batchTotal == 1 && summary_.active == 1) {
            const auto single_it = std::ranges::find_if(tasks_, [](const auto& t) {
                return t.state != TaskState::Completed && t.state != TaskState::Cancelled &&
                       t.state != TaskState::Failed;
            });
            if (single_it != tasks_.end()) {
                const auto ep = display::shortEpisode(single_it->number);
                if (summary_.transferring && single_it->bytesTotal > 0) {
                    text = ep + QStringLiteral(" · ") + percent(summary_.batchProgress);
                } else {
                    text = ep + QStringLiteral(" · ") + state;
                }
            }
        }
        if (text.isEmpty()) {
            text =
                state + QStringLiteral(" · %1/%2").arg(summary_.batchDone).arg(summary_.batchTotal);
        }
        const auto shown = figure(QStringLiteral("Descarga"), text, summary_.batchProgress);
        figures.append(waiting ? retryable(shown) : shown);
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
    if (const int failed = summary_.failed - (alone && own_failed ? 1 : 0); failed > 0) {
        figures.append(figure(QStringLiteral("Con error"),
                              failed == 1 ? QStringLiteral("1 episodio")
                                          : QStringLiteral("%1 episodios").arg(failed),
                              -1, true));
    }
    // When the next episode comes, once and as text: the header already says it airs.
    if (const auto start = premiere(); start.isValid()) {
        figures.append(
            figure(QStringLiteral("Estreno"), display::when(start, QDateTime::currentDateTime())));
    } else if (next_airing_ && airing()) {
        const auto now = QDateTime::currentDateTimeUtc();
        // Within the grace it keeps its hour: no "Ahora", as on AnimeHub Web.
        const auto at =
            next_airing_->late ? QStringLiteral("Retrasado") : display::when(next_airing_->at, now);
        figures.append(
            figure(QStringLiteral("Próximo episodio"),
                   at + QStringLiteral(" · ") + display::shortEpisode(next_airing_->number)));
    }
    return figures;
}

void AnimeView::report(const storage::Stored<void>& result) {
    if (!result) {
        qWarning("library change: %s", qPrintable(result.error().message));
        emit message(QStringLiteral("No se pudo guardar el cambio: ") +
                         display::storageError(result.error().message),
                     true);
    }
}

void AnimeView::joinLibrary() {
    if (inLibrary()) {
        return;
    }
    // What the page knows of the anime: its saved record, the card it was opened from, or the
    // answer of the API.
    library::AnimeRecord snapshot;
    if (record_) {
        snapshot = *record_;
    } else if (!hint_.slug.isEmpty()) {
        snapshot = hint_;
    } else if (detail_) {
        snapshot = library::snapshotOf(*detail_);
    }
    if (snapshot.slug.isEmpty()) {
        return;
    }
    const auto added = services_.library.add(snapshot);
    report(added);
    if (added) {
        emit addedToLibrary(slug_, title());
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
        if (single() && summary_.target) {
            retryDownload(*summary_.target);
        } else {
            retryFailed();
        }
        break;
    case PrimaryAction::DownloadAll:
        downloadMissing();
        break;
    case PrimaryAction::PauseAll:
        pauseSeries();
        break;
    case PrimaryAction::ResumeAll:
        resumeSeries();
        break;
    case PrimaryAction::Upcoming:
    case PrimaryAction::None:
        break;
    }
}

void AnimeView::setInLibrary(bool value) {
    if (value) {
        joinLibrary();
        return;
    }
    if (!record_) {
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
    const auto started = services_.player.play(
        {.slug = slug_,
         .number = number,
         .filePath = episode->filePath,
         .title = single() ? title() : title() + QStringLiteral(" · ") + display::episode(number),
         .startMs = episode->state == WatchState::Watching ? episode->positionMs : 0});
    if (!started) {
        if (!QFile::exists(episode->filePath)) {
            report(services_.library.repository().clearEpisodeFile(slug_, number));
            readLibrary();
        }
        emit message(display::storageError(started.error()), true);
        return;
    }
    joinLibrary();
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
    // Only what is queued joins the library: a refused download leaves it as it was.
    const auto queued =
        services_.downloads.enqueueMany(slug_, wanted, title(), downloads::TaskOrigin::Manual);
    if (!queued) {
        emit message(display::storageError(queued.error()), true);
        return;
    }
    joinLibrary();
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

void AnimeView::retryWaiting() {
    services_.downloads.retryWaiting(slug_);
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
            single() ? QStringLiteral("¿Cancelar la descarga de %1?").arg(title())
                     : QStringLiteral("¿Cancelar la descarga del episodio %1?")
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
        offerUndo(single() ? QStringLiteral("Marcado como visto")
                           : QStringLiteral("Todo marcado como visto"),
                  std::move(saved));
    }
}

void AnimeView::markAllUnwatched() {
    auto saved = saveStates([](const library::EpisodeRecord& episode) {
        return episode.state != WatchState::Unwatched;
    });
    const auto result = services_.library.markAllUnwatched(slug_);
    report(result);
    if (result) {
        offerUndo(single() ? QStringLiteral("Marcado como no visto")
                           : QStringLiteral("Todo marcado como no visto"),
                  std::move(saved));
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
        emit message(QStringLiteral("No se pudo guardar el cambio: ") +
                         display::storageError(trashed.error().message),
                     true);
    } else if (trashed->failed > 0) {
        emit message(QStringLiteral("No se pudo mover el archivo a la Papelera: está en uso."),
                     true);
    } else {
        offerRestore(single() ? QStringLiteral("Archivo en la Papelera")
                              : display::episode(number) + QStringLiteral(" en la Papelera"),
                     trashed->files);
    }
}

void AnimeView::trashWatched() {
    const auto trashed = services_.library.trashWatched(slug_);
    if (!trashed) {
        emit message(QStringLiteral("No se pudo guardar el cambio: ") +
                         display::storageError(trashed.error().message),
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
        offerRestore((single() ? QStringLiteral("Archivo en la Papelera · ")
                      : trashed->count == 1
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
            emit message(QStringLiteral("No se pudo guardar el cambio: ") +
                             display::storageError(restored.error().message),
                         true);
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
    if (services_.settings.isolated()) {
        return;
    }
    const auto episode = episodes_.episode(number);
    if (episode && !episode->filePath.isEmpty()) {
        if (services_.shell.reveal) {
            services_.shell.reveal(episode->filePath);
        } else {
            shell::reveal(episode->filePath);
        }
    }
}

bool AnimeView::hasFolder() const {
    const auto folder = slug_.isEmpty() ? QString() : folderPath();
    return !folder.isEmpty() && QDir(folder).exists();
}

void AnimeView::openFolder() {
    if (services_.settings.isolated()) {
        return;
    }
    if (hasFolder()) {
        if (services_.shell.open) {
            services_.shell.open(folderPath());
        } else {
            shell::open(folderPath());
        }
    }
}

void AnimeView::openTrailer() {
    if (services_.settings.isolated()) {
        return;
    }
    if (const auto url = trailerUrl(); url.isValid()) {
        if (services_.shell.openUrl) {
            services_.shell.openUrl(url);
        } else {
            QDesktopServices::openUrl(url);
        }
    }
}

void AnimeView::openSource() {
    if (services_.settings.isolated()) {
        return;
    }
    if (detail_ && detail_->sourceUrl.isValid() &&
        (detail_->sourceUrl.scheme() == u"https" || detail_->sourceUrl.scheme() == u"http")) {
        if (services_.shell.openUrl) {
            services_.shell.openUrl(detail_->sourceUrl);
        } else {
            QDesktopServices::openUrl(detail_->sourceUrl);
        }
    }
}

void AnimeView::setFavorite(bool value) {
    const bool joins = value && !inLibrary(); // marking something is a way of keeping it
    const auto result = services_.library.setFlag(slug_, library::Flag::Favorite, value);
    report(result);
    if (result && joins && inLibrary()) {
        emit addedToLibrary(slug_, title());
    }
}

void AnimeView::setNotify(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::Notify, value));
}

void AnimeView::setAutoDownload(bool value) {
    report(services_.library.setFlag(slug_, library::Flag::AutoDownload, value));
}

} // namespace animehub::app
