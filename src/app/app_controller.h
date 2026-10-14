#ifndef ANIMEHUB_APP_CONTROLLER_H
#define ANIMEHUB_APP_CONTROLLER_H

#include "anime_view.h"
#include "app_settings.h"
#include "catalog_view.h"
#include "downloads_view.h"
#include "library_model.h"

#include <QLocalServer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSystemTrayIcon>
#include <QtQmlIntegration>

#include <memory>

class QMenu;

namespace animehub::app {

struct AppPaths {
    QString data;      // library, settings, caches
    QString engines;   // aria2 and mpv shipped next to the executable
    QString downloads; // default download folder
};

// Builds and owns everything the window shows, and the app's life outside it: tray icon,
// notifications, closing and the single running instance.
class AppController final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by main()")

    Q_PROPERTY(AppSettings* settings READ settings CONSTANT)
    Q_PROPERTY(LibraryModel* library READ library CONSTANT)
    Q_PROPERTY(CatalogView* catalog READ catalog CONSTANT)
    Q_PROPERTY(DownloadsView* downloads READ downloads CONSTANT)
    Q_PROPERTY(AnimeView* libraryAnime READ libraryAnime CONSTANT)
    Q_PROPERTY(AnimeView* catalogAnime READ catalogAnime CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

public:
    // Fails (empty `problem()`, false return of `ready()`) when the library cannot be opened.
    explicit AppController(AppPaths paths, QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] bool ready() const {
        return database_ != nullptr;
    }
    [[nodiscard]] QString problem() const {
        return problem_;
    }
    [[nodiscard]] QNetworkAccessManager& network() {
        return network_;
    }
    // Another AnimeHub with the same data folder already runs: it was asked to show itself.
    [[nodiscard]] static bool handOffToRunningInstance(const QString& data_directory);
    void listenForOtherInstances(const QString& data_directory);

    [[nodiscard]] AppSettings* settings() {
        return settings_.get();
    }
    [[nodiscard]] LibraryModel* library() {
        return library_model_.get();
    }
    [[nodiscard]] CatalogView* catalog() {
        return catalog_view_.get();
    }
    [[nodiscard]] DownloadsView* downloads() {
        return downloads_view_.get();
    }
    [[nodiscard]] AnimeView* libraryAnime() {
        return library_anime_.get();
    }
    [[nodiscard]] AnimeView* catalogAnime() {
        return catalog_anime_.get();
    }
    [[nodiscard]] static QString version();

    void start();

    Q_INVOKABLE void openLibraryAnime(const QString& slug);
    Q_INVOKABLE void openCatalogAnime(const QString& slug);
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void closeTo(AppSettings::CloseAction action, bool remember);
    Q_INVOKABLE void quit();
    Q_INVOKABLE void openDownloadFolder();
    Q_INVOKABLE void openUrl(const QUrl& url);
    Q_INVOKABLE QString localPath(const QUrl& url) const;

signals:
    void showRequested();
    void hideRequested();
    void closeDecisionNeeded();
    // A notification was clicked: the window shows this anime in the library.
    void animeRequested(const QString& slug);
    void problemChanged();

private:
    void setupTray();
    void notifyNewEpisodes(const QString& slug, const QList<double>& numbers);
    void notify(const QString& title, const QString& text, const QString& slug);
    void hideToTray();

    AppPaths paths_;
    QNetworkAccessManager network_;
    std::unique_ptr<AppSettings> settings_;
    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<library::LibraryRepository> library_repository_;
    std::unique_ptr<downloads::DownloadRepository> download_repository_;
    std::unique_ptr<api::ApiClient> api_;
    std::unique_ptr<library::LibraryService> library_;
    std::unique_ptr<downloads::Aria2> aria2_;
    std::unique_ptr<downloads::mega::Api> mega_;
    std::unique_ptr<downloads::pixeldrain::Api> pixeldrain_;
    std::unique_ptr<downloads::ClickNLoad> click_n_load_;
    std::unique_ptr<downloads::DownloadManager> downloads_;
    std::unique_ptr<playback::Player> player_;
    std::unique_ptr<LibraryModel> library_model_;
    std::unique_ptr<CatalogView> catalog_view_;
    std::unique_ptr<DownloadsView> downloads_view_;
    std::unique_ptr<AnimeView> library_anime_;
    std::unique_ptr<AnimeView> catalog_anime_;
    std::unique_ptr<QSystemTrayIcon> tray_;
    std::unique_ptr<QMenu> tray_menu_;
    QLocalServer instance_server_;
    QString notified_slug_;
    QString problem_;
    bool quitting_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_CONTROLLER_H
