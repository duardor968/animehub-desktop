#ifndef ANIMEHUB_APP_ANIME_VIEW_H
#define ANIMEHUB_APP_ANIME_VIEW_H

#include "app_settings.h"
#include "downloads/click_n_load.h"
#include "downloads/download_manager.h"
#include "episode_model.h"
#include "library/library_service.h"
#include "playback/player.h"

#include <QObject>
#include <QUrl>
#include <QtQmlIntegration>

namespace animehub::app {

struct Services {
    api::ApiClient& api;
    library::LibraryService& library;
    downloads::DownloadManager& downloads;
    downloads::ClickNLoad& clickNLoad;
    playback::Player& player;
    AppSettings& settings;
};

// One anime's page: its details, the user's flags, the main action and the episode list. Works
// the same for library and catalog anime; acting on an episode keeps the anime in the library.
class AnimeView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppController")

    Q_PROPERTY(QString slug READ slug NOTIFY slugChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString alternativeTitle READ alternativeTitle NOTIFY changed)
    Q_PROPERTY(QString synopsis READ synopsis NOTIFY changed)
    Q_PROPERTY(QUrl posterUrl READ posterUrl NOTIFY changed)
    Q_PROPERTY(QUrl backdropUrl READ backdropUrl NOTIFY changed)
    Q_PROPERTY(QString facts READ facts NOTIFY changed)
    Q_PROPERTY(QString genres READ genres NOTIFY changed)
    Q_PROPERTY(bool inLibrary READ inLibrary NOTIFY changed)
    Q_PROPERTY(bool favorite READ favorite NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool notify READ notify NOTIFY changed)
    Q_PROPERTY(bool autoDownload READ autoDownload NOTIFY changed)
    Q_PROPERTY(QString primaryText READ primaryText NOTIFY changed)
    Q_PROPERTY(PrimaryAction primaryAction READ primaryAction NOTIFY changed)
    Q_PROPERTY(double resumeProgress READ resumeProgress NOTIFY changed)
    Q_PROPERTY(QString resumeText READ resumeText NOTIFY changed)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString downloadFolder READ downloadFolder NOTIFY changed)
    Q_PROPERTY(bool canSendToJDownloader READ canSendToJDownloader NOTIFY changed)
    Q_PROPERTY(EpisodeModel* episodes READ episodes CONSTANT)

public:
    enum class PrimaryAction { None, Play, Download, Wait };
    Q_ENUM(PrimaryAction)

    AnimeView(Services services, QObject* parent = nullptr);

    // Shows `slug`; `hint` (a catalog card) paints the header before the API answers.
    void open(const QString& slug, const library::AnimeRecord& hint = {});

    [[nodiscard]] QString slug() const {
        return slug_;
    }
    [[nodiscard]] bool loaded() const {
        return record_.has_value();
    }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString alternativeTitle() const;
    [[nodiscard]] QString synopsis() const;
    [[nodiscard]] QUrl posterUrl() const;
    [[nodiscard]] QUrl backdropUrl() const;
    [[nodiscard]] QString facts() const;
    [[nodiscard]] QString genres() const;
    [[nodiscard]] bool inLibrary() const;
    [[nodiscard]] bool favorite() const;
    [[nodiscard]] bool pending() const;
    [[nodiscard]] bool notify() const;
    [[nodiscard]] bool autoDownload() const;
    [[nodiscard]] QString primaryText() const;
    [[nodiscard]] PrimaryAction primaryAction() const;
    [[nodiscard]] double resumeProgress() const;
    [[nodiscard]] QString resumeText() const;
    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] QString problem() const {
        return problem_;
    }
    [[nodiscard]] QString downloadFolder() const;
    [[nodiscard]] bool canSendToJDownloader() const;
    [[nodiscard]] EpisodeModel* episodes() {
        return &episodes_;
    }

    Q_INVOKABLE void primary();
    Q_INVOKABLE void play(double number);
    Q_INVOKABLE void download(double number);
    Q_INVOKABLE void downloadUnwatched();
    Q_INVOKABLE void pauseDownload(double number);
    Q_INVOKABLE void resumeDownload(double number);
    Q_INVOKABLE void cancelDownload(double number);
    Q_INVOKABLE void retryDownload(double number);
    Q_INVOKABLE void markWatched(double number);
    Q_INVOKABLE void markUnwatched(double number);
    Q_INVOKABLE void markWatchedThrough(double number);
    Q_INVOKABLE void markAllWatched();
    Q_INVOKABLE void markAllUnwatched();
    Q_INVOKABLE void continueFrom(double number, const QString& clock);
    Q_INVOKABLE void trashFile(double number);
    Q_INVOKABLE void showFile(double number);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void sendToJDownloader(double number);
    Q_INVOKABLE void setInLibrary(bool value);
    Q_INVOKABLE void setFavorite(bool value);
    Q_INVOKABLE void setPending(bool value);
    Q_INVOKABLE void setNotify(bool value);
    Q_INVOKABLE void setAutoDownload(bool value);
    Q_INVOKABLE void reload();

signals:
    void slugChanged();
    void changed();
    // A short outcome for the user; `problem` marks failures.
    void message(const QString& text, bool problem);

private:
    void readLibrary();
    void report(const storage::Stored<void>& result);
    [[nodiscard]] QString folderPath() const;

    Services services_;
    QString slug_;
    library::AnimeRecord hint_;
    std::optional<library::LibraryEntry> record_;
    EpisodeModel episodes_;
    QString problem_;
    quint64 generation_ = 0;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_ANIME_VIEW_H
