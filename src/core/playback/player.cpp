#include "player.h"

#include "platform/installed_apps.h"
#include "platform/player_window.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace animehub::playback {
namespace {

constexpr int kConnectAttempts = 75; // 15 s for mpv to open its pipe
constexpr qint64 kSaveIntervalMs = 5000;
constexpr auto kPotPlayerSampleInterval = std::chrono::seconds(1);

// "/seek=hh:mm:ss", as PotPlayer takes a start position.
QString seekOption(qint64 ms) {
    const qint64 total = ms / 1000;
    return QStringLiteral("/seek=%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QByteArray command(const QJsonArray& arguments) {
    return QJsonDocument(QJsonObject{{QStringLiteral("command"), arguments}})
               .toJson(QJsonDocument::Compact) +
           '\n';
}

} // namespace

bool countsAsWatched(qint64 position_ms, qint64 duration_ms) {
    if (duration_ms <= 0 || position_ms <= 0) {
        return false;
    }
    const qint64 remaining = duration_ms - position_ms;
    return position_ms * 10 >= duration_ms * 9 ||
           (duration_ms > 10 * 60'000 && remaining <= 2 * 60'000);
}

struct Player::Session {
    PlayRequest request;
    QString pipeName;
    QLocalSocket socket;
    QByteArray buffer;
    qint64 positionMs = -1;
    qint64 durationMs = 0;
    qint64 savedPositionMs = -1;
    QDateTime savedAt;
    bool watched = false;
    bool reachedEnd = false;
    bool connected = false;
    int attempts = 0;
};

Player::Player(library::LibraryRepository& library, std::function<PlayerSettings()> settings,
               std::function<bool(const QString&)> open_with_system, QObject* parent)
    : QObject(parent), library_(library), settings_(std::move(settings)),
      open_with_system_(std::move(open_with_system)) {}

Player::~Player() {
    if (session_) {
        save(true);
        endSession(); // detach before the socket's destructor reports a disconnection
    }
    playlist_.reset(); // saves where it was
    playlist_exit_.reset();
}

bool Player::isTracking() const {
    return session_ != nullptr || playlist_ != nullptr;
}

std::expected<void, QString> Player::play(const PlayRequest& request) {
    if (!QFileInfo(request.filePath).isFile()) {
        return std::unexpected(QStringLiteral("El archivo ya no está en el disco."));
    }
    const auto settings = settings_();
    if (settings.kind == PlayerKind::Bundled) {
        return playWithMpv(request, settings);
    }
    if (settings.kind == PlayerKind::PotPlayer && !settings.potPlayerExecutable.isEmpty()) {
        return playWithPotPlayer(request, settings, settings.potPlayerExecutable);
    }
    // The system's player for this file is PotPlayer: it is followed as if chosen.
    if (const auto handler = settings.systemApplication
                                 ? settings.systemApplication(QFileInfo(request.filePath).suffix())
                                 : QString();
        platform::isPotPlayer(handler)) {
        return playWithPotPlayer(request, settings, handler);
    }
    if (!open_with_system_ || !open_with_system_(request.filePath)) {
#ifdef Q_OS_WIN
        return std::unexpected(
            QStringLiteral("Windows no tiene una aplicación asociada a este tipo de archivo."));
#else
        return std::unexpected(
            QStringLiteral("El sistema no tiene una aplicación asociada a este tipo de archivo."));
#endif
    }
    // Without a way to follow the system player, opening the episode counts as watching it.
    if (auto stored =
            library_.setWatchState(request.slug, request.number, library::WatchState::Watched);
        !stored) {
        return std::unexpected(stored.error().message);
    }
    emit progressChanged(request.slug, request.number);
    return {};
}

std::expected<void, QString> Player::playWithMpv(const PlayRequest& request,
                                                 const PlayerSettings& settings) {
    if (!QFileInfo(settings.mpvExecutable).isExecutable()) {
#ifdef Q_OS_WIN
        return std::unexpected(QStringLiteral("No se encuentra el reproductor incluido (mpv)."));
#else
        // Linux uses the system's mpv: the project publishes no Linux build to ship.
        return std::unexpected(QStringLiteral("Para reproducir con mpv, instala el paquete mpv o "
                                              "elige la aplicación predeterminada en Ajustes."));
#endif
    }
    endPlaylist();
    if (session_) {
        // One followed playback at a time: the previous one closes with its progress saved.
        save(true);
        if (session_->socket.state() == QLocalSocket::ConnectedState) {
            session_->socket.write(command({QStringLiteral("quit")}));
            session_->socket.flush();
        }
        endSession();
    }
    QDir().mkpath(settings.configDirectory);
    auto session = std::make_unique<Session>();
    session->request = request;
    const auto name = QStringLiteral("animehub-mpv-") + QUuid::createUuid().toString(QUuid::Id128);
#ifdef Q_OS_WIN
    session->pipeName = name;
    const auto server = QStringLiteral("\\\\.\\pipe\\") + name;
#else
    // A Unix socket in the user's private runtime folder; QLocalSocket takes its full path.
    session->pipeName =
        QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)).filePath(name);
    const auto server = session->pipeName;
#endif
    QStringList arguments = {
        QStringLiteral("--input-ipc-server=") + server,
        QStringLiteral("--force-window=immediate"),
        QStringLiteral("--keep-open=no"),
        QStringLiteral("--config-dir=") + QDir::toNativeSeparators(settings.configDirectory),
        QStringLiteral("--force-media-title=") + request.title,
    };
    if (request.startMs > 0) {
        arguments.append(QStringLiteral("--start=") +
                         QString::number(static_cast<double>(request.startMs) / 1000.0, 'f', 1));
    }
    // "--" ends the options: a file name can never be read as one.
    arguments << QStringLiteral("--") << QDir::toNativeSeparators(request.filePath);
    // Detached and without AnimeHub's standard handles: the video outlives AnimeHub and holds
    // nothing of it open.
    QProcess mpv;
    mpv.setProgram(settings.mpvExecutable);
    mpv.setArguments(arguments);
    mpv.setWorkingDirectory(QFileInfo(settings.mpvExecutable).absolutePath());
    mpv.setStandardInputFile(QProcess::nullDevice());
    mpv.setStandardOutputFile(QProcess::nullDevice());
    mpv.setStandardErrorFile(QProcess::nullDevice());
    if (!mpv.startDetached()) {
        return std::unexpected(QStringLiteral("No se pudo abrir el reproductor incluido (mpv)."));
    }
    session_ = std::move(session);
    auto* socket = &session_->socket;
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
        if (!session_ || &session_->socket != socket) {
            return;
        }
        session_->buffer += socket->readAll();
        qsizetype newline = 0;
        while ((newline = session_->buffer.indexOf('\n')) >= 0) {
            const auto line = session_->buffer.left(newline);
            session_->buffer.remove(0, newline + 1);
            handleMessage(line);
            if (!session_) {
                return;
            }
        }
    });
    connect(socket, &QLocalSocket::connected, this, [this, socket] {
        if (!session_ || &session_->socket != socket) {
            return;
        }
        session_->connected = true;
        socket->write(command({QStringLiteral("observe_property"), 1, QStringLiteral("time-pos")}));
        socket->write(command({QStringLiteral("observe_property"), 2, QStringLiteral("duration")}));
    });
    connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
        if (session_ && &session_->socket == socket) {
            save(true);
            endSession();
        }
    });
    // mpv opens its pipe a moment after starting: keep knocking for a while.
    connect(socket, &QLocalSocket::errorOccurred, this, [this, socket] {
        if (!session_ || &session_->socket != socket || session_->connected) {
            return;
        }
        if (++session_->attempts >= kConnectAttempts) {
            endSession(); // mpv never opened its pipe: it plays, unfollowed
            return;
        }
        QTimer::singleShot(200, this, [this, socket] {
            if (session_ && &session_->socket == socket && !session_->connected) {
                socket->connectToServer(session_->pipeName);
            }
        });
    });
    socket->connectToServer(session_->pipeName);
    return {};
}

PotPlayerLaunch potPlayerLaunch(library::LibraryRepository& library, const PlayRequest& request,
                                const QStringList& options) {
    QStringList files{QDir::toNativeSeparators(request.filePath)};
    PotPlayerLaunch launch{.folders = {QFileInfo(request.filePath).absolutePath()}};
    if (const auto episodes = library.episodes(request.slug)) {
        auto later = *episodes;
        std::ranges::sort(later, {}, &library::EpisodeRecord::number);
        for (const auto& episode : later) {
            if (episode.number <= request.number || episode.state == library::WatchState::Watched ||
                episode.filePath.isEmpty() || !QFileInfo(episode.filePath).isFile()) {
                continue;
            }
            files.append(QDir::toNativeSeparators(episode.filePath));
            if (const auto folder = QFileInfo(episode.filePath).absolutePath();
                !launch.folders.contains(folder)) {
                launch.folders.append(folder);
            }
        }
    }
    // Its own process, so the window followed is this playback's.
    launch.arguments = {QStringLiteral("/new")};
    if (request.startMs > 0) {
        launch.arguments.append(seekOption(request.startMs));
    }
    launch.arguments << options << files;
    return launch;
}

std::expected<void, QString> Player::playWithPotPlayer(const PlayRequest& request,
                                                       const PlayerSettings& settings,
                                                       const QString& executable) {
    auto launch = potPlayerLaunch(library_, request, settings.potPlayerOptions);
    QProcess potplayer;
    potplayer.setProgram(executable);
    potplayer.setArguments(launch.arguments);
    potplayer.setWorkingDirectory(QFileInfo(executable).absolutePath());
    potplayer.setStandardInputFile(QProcess::nullDevice());
    potplayer.setStandardOutputFile(QProcess::nullDevice());
    potplayer.setStandardErrorFile(QProcess::nullDevice());
    qint64 pid = 0;
    if (!potplayer.startDetached(&pid)) {
        return std::unexpected(QStringLiteral("No se pudo abrir PotPlayer."));
    }
    // One followed playback at a time.
    endPlaylist();
    if (session_) {
        save(true);
        endSession();
    }
    playlist_ = std::make_unique<PlaylistTracker>(
        library_, launch.folders, [pid] { return platform::samplePotPlayer(pid); },
        kPotPlayerSampleInterval);
    connect(playlist_.get(), &PlaylistTracker::progressChanged, this, &Player::progressChanged);
    playlist_exit_ = platform::watchExit(pid, [this] { endPlaylist(); });
    return {};
}

void Player::endPlaylist() {
    if (!playlist_) {
        return;
    }
    playlist_->finish();
    // Often called from the exit watch's own signal: both go once it returns.
    std::shared_ptr<PlaylistTracker> playlist(playlist_.release());
    std::shared_ptr<QObject> exit(playlist_exit_.release());
    QTimer::singleShot(0, QCoreApplication::instance(), [playlist, exit] {});
}

void Player::handleMessage(const QByteArray& line) {
    const auto message = QJsonDocument::fromJson(line).object();
    const auto event = message.value(u"event").toString();
    if (event == u"property-change") {
        const auto name = message.value(u"name").toString();
        const auto value = message.value(u"data");
        if (!value.isDouble()) {
            return;
        }
        if (name == u"time-pos") {
            session_->positionMs = static_cast<qint64>(value.toDouble() * 1000.0);
            save(false);
        } else if (name == u"duration") {
            session_->durationMs = static_cast<qint64>(value.toDouble() * 1000.0);
        }
    } else if (event == u"end-file" && message.value(u"reason").toString() == u"eof") {
        session_->reachedEnd = true;
        save(true);
    }
}

void Player::save(bool final_save) {
    auto& s = *session_;
    if (s.positionMs < 0 || s.watched) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    if (!final_save && s.savedAt.isValid() && s.savedAt.msecsTo(now) < kSaveIntervalMs &&
        s.savedPositionMs >= 0) {
        return;
    }
    s.savedAt = now;
    s.savedPositionMs = s.positionMs;
    const bool watched = s.reachedEnd || countsAsWatched(s.positionMs, s.durationMs);
    const auto stored =
        watched
            ? library_.setWatchState(s.request.slug, s.request.number, library::WatchState::Watched)
            : library_.setWatchState(s.request.slug, s.request.number,
                                     library::WatchState::Watching, s.positionMs, s.durationMs);
    if (!stored) {
        qWarning("playback: %s", qPrintable(stored.error().message));
        return;
    }
    s.watched = watched;
    emit progressChanged(s.request.slug, s.request.number);
}

void Player::endSession() {
    if (!session_) {
        return;
    }
    // Often called from the socket's own signal: the session dies once that signal returns.
    std::shared_ptr<Session> session(session_.release());
    session->socket.disconnect(this);
    session->socket.abort();
    QTimer::singleShot(0, QCoreApplication::instance(), [session] {});
}

} // namespace animehub::playback
