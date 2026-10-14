#ifndef ANIMEHUB_CORE_DOWNLOADS_CLICK_N_LOAD_H
#define ANIMEHUB_CORE_DOWNLOADS_CLICK_N_LOAD_H

#include <QObject>
#include <QUrl>

#include <expected>
#include <functional>

class QNetworkAccessManager;

namespace animehub::downloads {

// Hands links to a JDownloader running on this computer through Click'n'Load (port 9666). Used
// for the hosts AnimeHub does not download by itself (MP4Upload, 1Fichier).
class ClickNLoad final : public QObject {
    Q_OBJECT

public:
    enum class Failure {
        Unreachable, // nothing listens: JDownloader is closed or Click'n'Load is off
        Foreign,     // something else answers on the port
        Rejected,    // JDownloader refused the links
    };

    explicit ClickNLoad(QNetworkAccessManager& network, QObject* parent = nullptr,
                        QUrl base = QUrl(QStringLiteral("http://127.0.0.1:9666/flash/")));

    void send(const QString& package, const QList<QUrl>& links, QObject* context,
              std::function<void(const std::expected<void, Failure>&)> done);

private:
    QNetworkAccessManager& network_;
    QUrl base_;
};

} // namespace animehub::downloads

#endif // ANIMEHUB_CORE_DOWNLOADS_CLICK_N_LOAD_H
