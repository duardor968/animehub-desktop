#ifndef ANIMEHUB_CORE_DOWNLOADS_ARIA2_H
#define ANIMEHUB_CORE_DOWNLOADS_ARIA2_H

#include "platform/process_job.h"

#include <QByteArray>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <expected>
#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace animehub::downloads {

// One aria2 process driven over JSON-RPC on 127.0.0.1 with a random secret. URLs and the secret
// never appear on aria2's command line; the process dies with AnimeHub.
class Aria2 final : public QObject {
    Q_OBJECT

public:
    struct Transfer {
        enum class Status { Active, Waiting, Paused, Error, Complete, Removed, Unknown };
        QString gid;
        Status status = Status::Unknown;
        qint64 totalBytes = 0;
        qint64 completedBytes = 0;
        qint64 bytesPerSecond = 0;
        int errorCode = 0; // aria2 exit status of a failed transfer
        QString errorMessage;
    };

    struct Request {
        QUrl url;
        QString directory;
        QString fileName;
        int connections = 4;
    };

    using Done = std::function<void(const std::expected<QJsonValue, QString>&)>;

    Aria2(QNetworkAccessManager& network, QString executable, QByteArray user_agent,
          QObject* parent = nullptr);
    ~Aria2() override;

    void start();
    [[nodiscard]] bool isReady() const {
        return ready_;
    }

    // Answers with the transfer id (gid).
    void add(const Request& request, QObject* context,
             std::function<void(const std::expected<QString, QString>&)> done);
    void pause(const QString& gid);
    void resume(const QString& gid);
    // Stops the transfer and forgets it; the partial file and its control file stay on disk.
    void remove(const QString& gid);
    using StatusDone = std::function<void(const std::expected<QList<Transfer>, QString>&)>;
    void status(const QStringList& gids, QObject* context, StatusDone done);
    // Asks aria2 to save its control files and exit; `done` runs once it has (or after 6 s).
    void shutdown(std::function<void()> done);

signals:
    void ready();
    void failed(const QString& message);

private:
    void call(const QString& method, QJsonArray params, QObject* context, Done done);
    void waitUntilReady(int attempt);
    [[nodiscard]] QUrl endpoint() const;

    QNetworkAccessManager& network_;
    QString executable_;
    QByteArray user_agent_;
    QProcess process_;
    platform::ProcessJob job_;
    std::unique_ptr<QTemporaryDir> config_dir_;
    QByteArray secret_;
    quint16 port_ = 0;
    quint64 next_id_ = 0;
    bool ready_ = false;
    bool stopping_ = false;
};

} // namespace animehub::downloads

#endif // ANIMEHUB_CORE_DOWNLOADS_ARIA2_H
