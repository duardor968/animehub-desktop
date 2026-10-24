#include "app_controller.h"

#include "display.h"
#include "platform/trash.h"
#include "shell.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QMenu>

namespace animehub::app {
namespace {

QString instanceName(const QString& data_directory) {
    const auto key = QDir::cleanPath(QDir(data_directory).absolutePath()).toLower().toUtf8();
    return QStringLiteral("animehub-") +
           QString::fromLatin1(
               QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(16));
}

const QIcon& appIcon() {
    static const QIcon kIcon(QStringLiteral(":/qt/qml/AnimeHub/assets/animehub.png"));
    return kIcon;
}

} // namespace

QString AppController::version() {
    return QStringLiteral(ANIMEHUB_VERSION);
}

AppController::AppController(AppPaths paths, QObject* parent)
    : QObject(parent), paths_(std::move(paths)) {
    QDir().mkpath(paths_.data);
    settings_ = std::make_unique<AppSettings>(
        QDir(paths_.data).filePath(QStringLiteral("settings.ini")), paths_.downloads);
    settings_->setIsolated(paths_.isolated);
    auto database =
        storage::Database::open(QDir(paths_.data).filePath(QStringLiteral("library.sqlite3")));
    if (!database) {
        problem_ = QStringLiteral("No se pudo abrir la biblioteca: ") + database.error().message;
        return;
    }
    database_ = std::move(*database);
    library_repository_ = std::make_unique<library::LibraryRepository>(*database_);
    download_repository_ = std::make_unique<downloads::DownloadRepository>(*database_);
    if (paths_.isolated && paths_.transfers) {
        const auto tasks = download_repository_->all();
        const auto root = QDir::cleanPath(QDir(paths_.data).absolutePath()) + u'/';
        if (!tasks || std::ranges::any_of(*tasks, [&root](const downloads::Task& task) {
                return !QDir::cleanPath(QFileInfo(task.targetPath).absoluteFilePath())
                            .startsWith(root, Qt::CaseInsensitive);
            })) {
            problem_ = QStringLiteral("El perfil de prueba contiene destinos externos. Usa un "
                                      "perfil limpio para habilitar transferencias.");
            return;
        }
    }

    api::ApiClient::Options api_options;
    api_options.cacheDirectory = QDir(paths_.data).filePath(QStringLiteral("cache/api"));
    api_options.userAgent = "AnimeHub-Desktop/" + version().toLatin1();
    if (const auto base = qEnvironmentVariable("ANIMEHUB_API_URL"); !base.isEmpty()) {
        api_options.baseUrl = QUrl(base);
    }
    if (!paths_.apiBase.isEmpty())
        api_options.baseUrl = paths_.apiBase;
    api_ = std::make_unique<api::ApiClient>(network_, api_options);
    api::ResponseCache(api_options.cacheDirectory).prune(std::chrono::hours(24 * 30));
    library_ = std::make_unique<library::LibraryService>(
        *library_repository_, *api_, nullptr,
        [this](const QString& path) -> std::optional<QString> {
            if (paths_.isolated)
                return std::nullopt;
            return platform::moveToTrash(path);
        },
        [this](const QString& source, const QString& destination) {
            return !paths_.isolated && platform::restoreFromTrash(source, destination);
        });

    aria2_ = std::make_unique<downloads::Aria2>(
        network_, QDir(paths_.engines).filePath(QStringLiteral("aria2/aria2c.exe")),
        api_options.userAgent);
    mega_ = std::make_unique<downloads::mega::Api>(network_);
    pixeldrain_ = std::make_unique<downloads::pixeldrain::Api>(network_);
    click_n_load_ = std::make_unique<downloads::ClickNLoad>(network_);
    myjd_ = std::make_unique<downloads::myjd::Client>(network_);
    // One saved account per data folder, so test profiles never touch the real one.
    const auto credential = QStringLiteral("AnimeHub/MyJDownloader/") + instanceName(paths_.data);
    jdownloader_view_ =
        std::make_unique<JDownloaderView>(*settings_, *click_n_load_, *myjd_, credential);
    downloads_ = std::make_unique<downloads::DownloadManager>(
        downloads::DownloadManager::Services{*api_, *aria2_, *mega_, *pixeldrain_,
                                             *download_repository_, *library_repository_,
                                             myjd_.get()},
        [this] { return settings_->downloads(); });
    remote_service_ = std::make_unique<remote::RemoteService>(
        network_, api_options.baseUrl, paths_.data,
        [this](const QString& slug, std::function<void(remote::Readiness)> done) {
            // A profile without transfers would accept requests it can never queue.
            if (!paths_.transfers) {
                done(remote::Readiness::Never);
                return;
            }
            remote::rememberAnime(*api_, *library_, slug, this, std::move(done));
        },
        [this](const remote::Submission& submission) {
            if (!paths_.transfers)
                return false;
            auto fixed = settings_->downloads();
            fixed.audio = submission.profile.audio == u"dub" ? api::Audio::Dub : api::Audio::Sub;
            fixed.directory = submission.profile.destination;
            fixed.engine = submission.profile.engine == u"direct"
                               ? downloads::DownloadEngine::Native
                           : submission.profile.connection == u"cloud"
                               ? downloads::DownloadEngine::JDownloaderCloud
                               : downloads::DownloadEngine::JDownloaderLocal;
            if (fixed.engine == downloads::DownloadEngine::JDownloaderLocal)
                fixed.jdEndpoint = QUrl(submission.profile.connection);
            fixed.jdDeviceId = submission.profile.device;
            // The request was accepted once its anime was saved (rememberAnime): its title
            // names the files, and the library can record them. Sent from the web, it joins the
            // library, as "Descargar" on its page does.
            const auto anime = library_repository_->anime(submission.animeSlug);
            if (!anime || !*anime)
                return false;
            if (!(*anime)->inLibrary && !library_->add(**anime))
                return false;
            return downloads_
                ->enqueueMany(submission.animeSlug, submission.episodes, (*anime)->title,
                              downloads::TaskOrigin::Manual, submission.requestId, fixed)
                .has_value();
        });
    remote_view_ = std::make_unique<RemoteView>(*remote_service_);
    publishRemoteProfiles();
    connect(settings_.get(), &AppSettings::changed, this, &AppController::publishRemoteProfiles);
    player_ = std::make_unique<playback::Player>(
        *library_repository_,
        [this] {
            return playback::PlayerSettings{
                .kind = settings_->systemPlayer() ? playback::PlayerKind::System
                                                  : playback::PlayerKind::Bundled,
                .mpvExecutable = QDir(paths_.engines).filePath(QStringLiteral("mpv/mpv.exe")),
                .configDirectory = QDir(paths_.data).filePath(QStringLiteral("mpv"))};
        },
        [](const QString& path) { return shell::open(path); });
    connect(player_.get(), &playback::Player::progressChanged, library_.get(),
            &library::LibraryService::announce);

    const Services services{*api_,    *library_, *downloads_, *jdownloader_view_,
                            *player_, *settings_};
    library_model_ = std::make_unique<LibraryModel>(*library_, *downloads_, *api_);
    catalog_view_ = std::make_unique<CatalogView>(*api_, *library_model_);
    downloads_view_ = std::make_unique<DownloadsView>(*downloads_, *library_, *player_);
    library_anime_ = std::make_unique<AnimeView>(services);
    catalog_anime_ = std::make_unique<AnimeView>(services);

    connect(library_.get(), &library::LibraryService::newEpisodes, this,
            &AppController::notifyNewEpisodes);
    connect(library_.get(), &library::LibraryService::newFranchiseMembers, this,
            [this](const QStringList& slugs) {
                if (slugs.isEmpty())
                    return;
                QStringList titles;
                for (const auto& slug : slugs) {
                    if (const auto entry = library_repository_->entry(slug); entry && *entry)
                        titles.append((*entry)->title);
                }
                if (!titles.isEmpty())
                    notify(slugs.size() == 1 ? QStringLiteral("Nueva entrega en tu biblioteca")
                                             : QStringLiteral("Nuevas entregas en tu biblioteca"),
                           titles.join(QStringLiteral(" · ")), slugs.first());
            });
    connect(downloads_.get(), &downloads::DownloadManager::taskCompleted, this, [this](qint64 id) {
        const auto task = downloads_->task(id);
        library_->announce(task->slug);
        notify(QStringLiteral("Descarga completada"),
               task->animeTitle + QStringLiteral(" · ") + display::episode(task->number),
               task->slug);
    });
    connect(downloads_.get(), &downloads::DownloadManager::taskFailed, this, [this](qint64 id) {
        const auto task = downloads_->task(id);
        notify(QStringLiteral("Descarga detenida"),
               task->animeTitle + QStringLiteral(" · ") + display::episode(task->number) +
                   QStringLiteral(". ") + display::issue(task->issue),
               task->slug);
    });
}

AppController::~AppController() {
    if (tray_) {
        tray_->hide();
    }
}

void AppController::start() {
    if (!ready()) {
        return;
    }
    if (!paths_.smoke && paths_.transfers) {
        library_->verifyFiles();
        library_->startWatching();
    }
    downloads_->start(paths_.transfers);
    if (!paths_.smoke)
        jdownloader_view_->start();
    library_model_->start();
    if (!paths_.smoke) {
        remote_service_->start();
        setupTray();
    }
}

void AppController::publishRemoteProfiles() {
    const auto config = settings_->downloads();
    const bool jd = config.engine != downloads::DownloadEngine::Native;
    const QString connection = !jd ? QString()
                               : config.engine == downloads::DownloadEngine::JDownloaderCloud
                                   ? QStringLiteral("cloud")
                                   : config.jdEndpoint.toString();
    const QString device = jd ? config.jdDeviceId : QString();
    const auto audio =
        config.audio == api::Audio::Dub ? QStringLiteral("dub") : QStringLiteral("sub");
    // As Ajustes names the engines. The label is part of the identity: a new one is a new
    // revision, which the API takes in place of the old.
    const auto label = jd ? QStringLiteral("JDownloader") : QStringLiteral("AnimeHub");
    const QJsonArray identity{jd, config.directory, connection, device, audio, label};
    const auto fingerprint =
        QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex();
    const int revision = settings_->remoteProfileRevision(fingerprint);
    remote_service_->setProfiles(revision,
                                 {{QStringLiteral("default"), label,
                                   jd ? QStringLiteral("jdownloader") : QStringLiteral("direct"),
                                   config.directory, connection, device, true, audio}});
}

bool AppController::handOffToRunningInstance(const QString& data_directory, bool show) {
    QLocalSocket socket;
    socket.connectToServer(instanceName(data_directory));
    if (!socket.waitForConnected(500)) {
        return false;
    }
    if (show) {
        socket.write("show\n");
        socket.waitForBytesWritten(500);
    }
    return true;
}

void AppController::listenForOtherInstances(const QString& data_directory) {
    const auto name = instanceName(data_directory);
    QLocalServer::removeServer(name); // a crashed instance may have left its name behind
    instance_server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!instance_server_.listen(name)) {
        return;
    }
    // Another start says "show"; one started in the tray only checks this one is here.
    connect(&instance_server_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = instance_server_.nextPendingConnection()) {
            const auto read = [this, socket] {
                if (socket->readAll().contains("show")) {
                    emit showRequested();
                }
            };
            connect(socket, &QLocalSocket::readyRead, this, read);
            // A start that wrote and quit at once may only be read as it goes.
            connect(socket, &QLocalSocket::disconnected, this, read);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

void AppController::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_menu_ = std::make_unique<QMenu>();
    tray_menu_->setDefaultAction(tray_menu_->addAction(QStringLiteral("Abrir AnimeHub"), this,
                                                       &AppController::showRequested));
    // As the Downloads page's button: pausing goes first while anything can be paused.
    auto* pause = tray_menu_->addAction(QStringLiteral("Pausar las descargas"));
    connect(pause, &QAction::triggered, this, [this] {
        if (downloads_view_->canPauseAll()) {
            downloads_view_->pauseAll();
        } else {
            downloads_view_->resumeAll();
        }
    });
    connect(tray_menu_.get(), &QMenu::aboutToShow, this, [this, pause] {
        pause->setText(downloads_view_->canPauseAll() ? QStringLiteral("Pausar las descargas")
                                                      : QStringLiteral("Reanudar las descargas"));
        pause->setEnabled(downloads_view_->canPauseAll() || downloads_view_->canResumeAll());
    });
    tray_menu_->addSeparator();
    tray_menu_->addAction(QStringLiteral("Salir"), this, &AppController::quit);

    tray_ = std::make_unique<QSystemTrayIcon>(appIcon());
    tray_->setToolTip(QStringLiteral("AnimeHub"));
    tray_->setContextMenu(tray_menu_.get());
    connect(tray_.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    emit showRequested();
                }
            });
    connect(tray_.get(), &QSystemTrayIcon::messageClicked, this, [this] {
        emit showRequested();
        if (!notified_slug_.isEmpty()) {
            emit animeRequested(notified_slug_);
        }
    });
    tray_->show();
}

void AppController::notify(const QString& title, const QString& text, const QString& slug) {
    if (!tray_ || !settings_->notifications()) {
        return;
    }
    notified_slug_ = slug;
    tray_->showMessage(title, text, appIcon(), 8000);
}

void AppController::notifyNewEpisodes(const QString& slug, const QList<double>& numbers) {
    const auto entry = library_repository_->entry(slug);
    if (!entry || !*entry || numbers.isEmpty()) {
        return;
    }
    const auto& anime = **entry;
    if (anime.notify) {
        notify(numbers.size() == 1 ? QStringLiteral("Episodio nuevo")
                                   : QStringLiteral("%1 episodios nuevos").arg(numbers.size()),
               anime.title + QStringLiteral(" · ") + display::episode(numbers.last()), slug);
    }
    if (anime.autoDownload) {
        for (const auto number : numbers) {
            if (const auto queued = downloads_->enqueue(slug, number, anime.title,
                                                        downloads::TaskOrigin::Automatic);
                !queued) {
                qWarning("auto download: %s", qPrintable(queued.error()));
            }
        }
    }
}

void AppController::openLibraryAnime(const QString& slug) {
    library_anime_->open(slug);
}

void AppController::openCatalogAnime(const QString& slug) {
    catalog_anime_->open(slug, catalog_view_->hint(slug));
}

void AppController::continueWatching(const QString& slug) {
    const auto entry = library_repository_->entry(slug);
    if (!entry || !*entry || !(*entry)->resumeNumber) {
        return;
    }
    const auto& anime = **entry;
    const auto number = *anime.resumeNumber;
    const auto episode = library_repository_->episode(slug, number);
    if (episode && *episode && !(*episode)->filePath.isEmpty()) {
        const auto& file = **episode;
        const auto started = player_->play(
            {.slug = slug,
             .number = number,
             .filePath = file.filePath,
             .title = anime.title + QStringLiteral(" · ") + display::episode(number),
             .startMs = file.state == library::WatchState::Watching ? file.positionMs : 0});
        if (!started) {
            if (!QFile::exists(file.filePath) &&
                library_repository_->clearEpisodeFile(slug, number)) {
                library_->announce(slug);
            }
            emit message(started.error(), true);
        }
        return;
    }
    if (const auto queued =
            downloads_->enqueue(slug, number, anime.title, downloads::TaskOrigin::Manual);
        !queued) {
        emit message(queued.error(), true);
    }
}

void AppController::requestClose() {
    switch (settings_->closeAction()) {
    case AppSettings::CloseAction::Ask:
        if (tray_) {
            emit closeDecisionNeeded();
            return;
        }
        quit();
        return;
    case AppSettings::CloseAction::Tray:
        hideToTray();
        return;
    case AppSettings::CloseAction::Quit:
        quit();
        return;
    }
}

void AppController::closeTo(AppSettings::CloseAction action, bool remember) {
    if (remember) {
        settings_->setCloseAction(action);
    }
    if (action == AppSettings::CloseAction::Tray) {
        hideToTray();
    } else {
        quit();
    }
}

void AppController::hideToTray() {
    if (!tray_) {
        quit();
        return;
    }
    emit hideRequested();
    // Once the choice is saved the dialog no longer says where the window went: Windows does,
    // the first time.
    if (settings_->closeAction() == AppSettings::CloseAction::Tray &&
        !settings_->trayNoticeShown()) {
        settings_->setTrayNoticeShown();
        tray_->showMessage(QStringLiteral("AnimeHub sigue en la bandeja"),
                           QStringLiteral("Puedes cambiarlo en Ajustes"), appIcon(), 6000);
    }
}

void AppController::quit() {
    if (quitting_) {
        return;
    }
    quitting_ = true;
    emit hideRequested();
    if (tray_) {
        tray_->hide();
    }
    if (!ready()) {
        QCoreApplication::quit();
        return;
    }
    downloads_->shutdown([] { QCoreApplication::quit(); });
}

void AppController::openDownloadFolder() {
    const auto folder = settings_->downloadDirectory();
    QDir().mkpath(folder);
    shell::open(folder);
}

void AppController::openUrl(const QUrl& url) {
    QDesktopServices::openUrl(url);
}

void AppController::tintTitleBar(QWindow* window, const QColor& background, const QColor& text) {
    shell::tintTitleBar(window, background, text);
}

QString AppController::localPath(const QUrl& url) const {
    return QDir::toNativeSeparators(url.toLocalFile());
}

} // namespace animehub::app
