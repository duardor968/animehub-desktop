#include "app_controller.h"

#include "display.h"
#include "shell.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
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
    static const QIcon icon(QStringLiteral(":/qt/qml/AnimeHub/assets/animehub.png"));
    return icon;
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
    auto database =
        storage::Database::open(QDir(paths_.data).filePath(QStringLiteral("library.sqlite3")));
    if (!database) {
        problem_ = QStringLiteral("No se pudo abrir la biblioteca: ") + database.error().message;
        return;
    }
    database_ = std::move(*database);
    library_repository_ = std::make_unique<library::LibraryRepository>(*database_);
    download_repository_ = std::make_unique<downloads::DownloadRepository>(*database_);

    api::ApiClient::Options api_options;
    api_options.cacheDirectory = QDir(paths_.data).filePath(QStringLiteral("cache/api"));
    api_options.userAgent = "AnimeHub-Desktop/" + version().toLatin1();
    if (const auto base = qEnvironmentVariable("ANIMEHUB_API_URL"); !base.isEmpty()) {
        api_options.baseUrl = QUrl(base);
    }
    api_ = std::make_unique<api::ApiClient>(network_, api_options);
    api::ResponseCache(api_options.cacheDirectory).prune(std::chrono::hours(24 * 30));
    library_ = std::make_unique<library::LibraryService>(*library_repository_, *api_);

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
                                             *download_repository_, *library_repository_},
        [this] { return settings_->downloads(); });
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
    library_model_ = std::make_unique<LibraryModel>(*library_);
    catalog_view_ = std::make_unique<CatalogView>(*api_);
    downloads_view_ = std::make_unique<DownloadsView>(*downloads_, *library_repository_, *player_);
    library_anime_ = std::make_unique<AnimeView>(services);
    catalog_anime_ = std::make_unique<AnimeView>(services);

    connect(library_.get(), &library::LibraryService::newEpisodes, this,
            &AppController::notifyNewEpisodes);
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
    library_->verifyFiles();
    library_->startWatching();
    downloads_->start();
    jdownloader_view_->start();
    setupTray();
}

bool AppController::handOffToRunningInstance(const QString& data_directory) {
    QLocalSocket socket;
    socket.connectToServer(instanceName(data_directory));
    if (!socket.waitForConnected(500)) {
        return false;
    }
    socket.write("show\n");
    socket.waitForBytesWritten(500);
    return true;
}

void AppController::listenForOtherInstances(const QString& data_directory) {
    const auto name = instanceName(data_directory);
    QLocalServer::removeServer(name); // a crashed instance may have left its name behind
    instance_server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!instance_server_.listen(name)) {
        return;
    }
    connect(&instance_server_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = instance_server_.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            emit showRequested();
        }
    });
}

void AppController::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_menu_ = std::make_unique<QMenu>();
    tray_menu_->addAction(QStringLiteral("Abrir AnimeHub"), this, &AppController::showRequested);
    auto* pause = tray_menu_->addAction(QStringLiteral("Pausar las descargas"));
    connect(pause, &QAction::triggered, this, [this] {
        if (downloads_view_->anyPaused()) {
            downloads_->resumeAll();
        } else {
            downloads_->pauseAll();
        }
    });
    connect(tray_menu_.get(), &QMenu::aboutToShow, this, [this, pause] {
        pause->setText(downloads_view_->anyPaused() ? QStringLiteral("Reanudar las descargas")
                                                    : QStringLiteral("Pausar las descargas"));
        pause->setEnabled(downloads_view_->activeCount() > 0);
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
    if (!settings_->value(QStringLiteral("trayHintShown")).toBool()) {
        settings_->setValue(QStringLiteral("trayHintShown"), true);
        tray_->showMessage(QStringLiteral("AnimeHub sigue abierto"),
                           QStringLiteral("Las descargas y los avisos continúan desde aquí."),
                           appIcon(), 6000);
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

QString AppController::localPath(const QUrl& url) const {
    return QDir::toNativeSeparators(url.toLocalFile());
}

} // namespace animehub::app
