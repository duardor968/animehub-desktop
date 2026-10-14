#include "aria2.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>

namespace animehub::downloads {
namespace {

constexpr int kReadyAttempts = 100; // 10 s
constexpr qint64 kMaxRpcBody = 4 * 1024 * 1024;

QByteArray randomSecret() {
    QByteArray bytes(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()),
                                          bytes.size() / 4);
    return bytes.toHex();
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return probe.serverPort();
}

Aria2::Transfer::Status statusFrom(const QString& text) {
    using S = Aria2::Transfer::Status;
    if (text == u"active") {
        return S::Active;
    }
    if (text == u"waiting") {
        return S::Waiting;
    }
    if (text == u"paused") {
        return S::Paused;
    }
    if (text == u"error") {
        return S::Error;
    }
    if (text == u"complete") {
        return S::Complete;
    }
    if (text == u"removed") {
        return S::Removed;
    }
    return S::Unknown;
}

} // namespace

Aria2::Aria2(QNetworkAccessManager& network, QString executable, QByteArray user_agent,
             QObject* parent)
    : QObject(parent), network_(network), executable_(std::move(executable)),
      user_agent_(std::move(user_agent)) {
    platform::hideConsoleWindow(process_);
    process_.setProcessChannelMode(QProcess::MergedChannels);
    process_.setStandardOutputFile(QProcess::nullDevice());
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!stopping_ && error == QProcess::FailedToStart) {
            ready_ = false;
            emit failed(QStringLiteral("aria2 could not start: %1").arg(process_.errorString()));
        }
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (!stopping_) {
            ready_ = false;
            emit failed(QStringLiteral("aria2 stopped (exit code %1)").arg(code));
        }
    });
}

Aria2::~Aria2() {
    stopping_ = true;
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(3000);
    }
}

QUrl Aria2::endpoint() const {
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(port_);
    url.setPath(QStringLiteral("/jsonrpc"));
    return url;
}

void Aria2::start() {
    if (process_.state() != QProcess::NotRunning) {
        return;
    }
    stopping_ = false;
    ready_ = false;
    port_ = freeLocalPort();
    secret_ = randomSecret();
    config_dir_ = std::make_unique<QTemporaryDir>();
    if (port_ == 0 || !config_dir_->isValid()) {
        emit failed(QStringLiteral("no local port or temporary folder for aria2"));
        return;
    }
    // Options go through a private file instead of the command line, so the RPC secret is not
    // visible in the process list; the file is deleted once aria2 has read it.
    const auto config_path = config_dir_->filePath(QStringLiteral("aria2.conf"));
    QFile config(config_path);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit failed(QStringLiteral("cannot write the aria2 configuration"));
        return;
    }
    QStringList options = {
        QStringLiteral("enable-rpc=true"),
        QStringLiteral("rpc-listen-all=false"),
        QStringLiteral("rpc-listen-port=%1").arg(port_),
        QStringLiteral("rpc-secret=%1").arg(QString::fromLatin1(secret_)),
        QStringLiteral("rpc-max-request-size=4M"),
        QStringLiteral("stop-with-process=%1").arg(QCoreApplication::applicationPid()),
        QStringLiteral("max-concurrent-downloads=8"),
        QStringLiteral("continue=true"),
        QStringLiteral("auto-file-renaming=false"),
        QStringLiteral("allow-overwrite=false"),
        QStringLiteral("file-allocation=none"),
        QStringLiteral("auto-save-interval=1"), // a crash or forced stop loses one second
        QStringLiteral("min-split-size=4M"),
        QStringLiteral("max-tries=5"),
        QStringLiteral("retry-wait=5"),
        QStringLiteral("connect-timeout=30"),
        QStringLiteral("timeout=60"),
        QStringLiteral("remote-time=false"),
        QStringLiteral("check-certificate=true"),
        QStringLiteral("console-log-level=warn"),
        QStringLiteral("summary-interval=0"),
        QStringLiteral("show-console-readout=false"),
        QStringLiteral("user-agent=%1").arg(QString::fromLatin1(user_agent_)),
    };
    // Troubleshooting: ANIMEHUB_ARIA2_LOG=<file> keeps aria2's own log.
    if (const auto log = qEnvironmentVariable("ANIMEHUB_ARIA2_LOG"); !log.isEmpty()) {
        options << QStringLiteral("log=%1").arg(log) << QStringLiteral("log-level=info");
    }
    config.write(options.join(QLatin1Char('\n')).toUtf8());
    config.close();

    process_.setProgram(executable_);
    process_.setArguments({QStringLiteral("--conf-path=%1").arg(config_path)});
    process_.start();
    if (!process_.waitForStarted(5000)) {
        return; // errorOccurred reports it
    }
    job_.assign(process_.processId());
    waitUntilReady(0);
}

void Aria2::waitUntilReady(int attempt) {
    call(QStringLiteral("aria2.getVersion"), {}, this,
         [this, attempt](const std::expected<QJsonValue, QString>& result) {
             if (result) {
                 ready_ = true;
                 config_dir_.reset();
                 emit ready();
                 return;
             }
             if (stopping_ || process_.state() != QProcess::Running) {
                 return;
             }
             if (attempt >= kReadyAttempts) {
                 emit failed(QStringLiteral("aria2 did not answer: %1").arg(result.error()));
                 return;
             }
             QTimer::singleShot(100, this, [this, attempt] { waitUntilReady(attempt + 1); });
         });
}

void Aria2::call(const QString& method, QJsonArray params, QObject* context, Done done) {
    params.prepend(QStringLiteral("token:") + QString::fromLatin1(secret_));
    const QJsonObject body{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), QString::number(++next_id_)},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("params"), params}};
    QNetworkRequest request(endpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setTransferTimeout(10'000);
    auto* reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] {
        const auto document = QJsonDocument::fromJson(reply->read(kMaxRpcBody));
        const auto object = document.object();
        if (object.contains(u"result")) {
            done(object.value(u"result"));
            return;
        }
        const auto error = object.value(u"error").toObject().value(u"message").toString();
        done(std::unexpected(error.isEmpty() ? reply->errorString() : error));
    });
}

void Aria2::add(const Request& request, QObject* context,
                std::function<void(const std::expected<QString, QString>&)> done) {
    const QJsonObject options{
        {QStringLiteral("dir"), request.directory},
        {QStringLiteral("out"), request.fileName},
        {QStringLiteral("max-connection-per-server"), QString::number(request.connections)},
        {QStringLiteral("split"), QString::number(request.connections)},
    };
    call(QStringLiteral("aria2.addUri"),
         {QJsonArray{request.url.toString(QUrl::FullyEncoded)}, options}, context,
         [done = std::move(done)](const std::expected<QJsonValue, QString>& result) {
             if (!result) {
                 done(std::unexpected(result.error()));
                 return;
             }
             done(result->toString());
         });
}

void Aria2::pause(const QString& gid) {
    call(QStringLiteral("aria2.forcePause"), {gid}, this, [](const auto&) {});
}

void Aria2::resume(const QString& gid) {
    call(QStringLiteral("aria2.unpause"), {gid}, this, [](const auto&) {});
}

void Aria2::remove(const QString& gid, QObject* context, std::function<void()> stopped) {
    // The callback runs only while its context lives.
    auto guarded = [context = QPointer<QObject>(context), stopped = std::move(stopped)] {
        if (stopped && context) {
            stopped();
        }
    };
    call(QStringLiteral("aria2.forceRemove"), {gid}, this,
         [this, gid, guarded = std::move(guarded)](const auto&) mutable {
             waitStopped(gid, 50, std::move(guarded));
         });
}

// aria2 stops a removed transfer on a later turn of its loop and only then closes its files.
void Aria2::waitStopped(const QString& gid, int attempts_left, std::function<void()> stopped) {
    status({gid}, this,
           [this, gid, attempts_left, stopped = std::move(stopped)](const auto& result) mutable {
               using S = Transfer::Status;
               const bool running =
                   result && !result->isEmpty() &&
                   (result->first().status == S::Active || result->first().status == S::Waiting ||
                    result->first().status == S::Paused);
               if (running && attempts_left > 0) {
                   QTimer::singleShot(100, this,
                                      [this, gid, attempts_left, stopped = std::move(stopped)] {
                                          waitStopped(gid, attempts_left - 1, stopped);
                                      });
                   return;
               }
               call(QStringLiteral("aria2.removeDownloadResult"), {gid}, this, [](const auto&) {});
               stopped();
           });
}

void Aria2::shutdown(std::function<void()> done) {
    stopping_ = true;
    ready_ = false;
    if (process_.state() == QProcess::NotRunning) {
        done();
        return;
    }
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, finished, done = std::move(done)] {
        if (*finished) {
            return;
        }
        *finished = true;
        if (process_.state() != QProcess::NotRunning) {
            qWarning("aria2 did not exit in time; its last progress may not be saved");
            process_.kill();
            process_.waitForFinished(1000);
        }
        done();
    };
    connect(&process_, &QProcess::finished, this, finish, Qt::SingleShotConnection);
    // forceShutdown writes the control files that let transfers resume; aria2 then waits three
    // seconds on purpose before exiting.
    call(QStringLiteral("aria2.forceShutdown"), {}, this, [](const auto&) {});
    QTimer::singleShot(6000, this, finish);
}

void Aria2::status(const QStringList& gids, QObject* context, StatusDone done) {
    if (gids.isEmpty()) {
        done(QList<Transfer>());
        return;
    }
    static const QJsonArray kKeys{
        QStringLiteral("gid"),           QStringLiteral("status"),
        QStringLiteral("totalLength"),   QStringLiteral("completedLength"),
        QStringLiteral("downloadSpeed"), QStringLiteral("errorCode"),
        QStringLiteral("errorMessage")};
    QJsonArray calls;
    const auto token = QStringLiteral("token:") + QString::fromLatin1(secret_);
    for (const auto& gid : gids) {
        calls.append(QJsonObject{{QStringLiteral("methodName"), QStringLiteral("aria2.tellStatus")},
                                 {QStringLiteral("params"), QJsonArray{token, gid, kKeys}}});
    }
    // system.multicall takes no token of its own: each call carries it. (`QJsonArray{calls}`
    // would copy `calls` instead of nesting it.)
    QJsonArray params;
    params.append(calls);
    const QJsonObject body{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), QString::number(++next_id_)},
                           {QStringLiteral("method"), QStringLiteral("system.multicall")},
                           {QStringLiteral("params"), params}};
    QNetworkRequest request(endpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setTransferTimeout(10'000);
    auto* reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context, [reply, gids, done = std::move(done)] {
        const auto object = QJsonDocument::fromJson(reply->read(kMaxRpcBody)).object();
        if (reply->error() != QNetworkReply::NoError || !object.value(u"result").isArray()) {
            done(std::unexpected(reply->errorString()));
            return;
        }
        const auto results = object.value(u"result").toArray();
        QList<Transfer> transfers;
        for (qsizetype index = 0; index < gids.size(); ++index) {
            Transfer transfer;
            transfer.gid = gids[index];
            const auto entry = results.at(index);
            if (entry.isArray()) {
                const auto fields = entry.toArray().first().toObject();
                transfer.status = statusFrom(fields.value(u"status").toString());
                transfer.totalBytes = fields.value(u"totalLength").toString().toLongLong();
                transfer.completedBytes = fields.value(u"completedLength").toString().toLongLong();
                transfer.bytesPerSecond = fields.value(u"downloadSpeed").toString().toLongLong();
                transfer.errorCode = fields.value(u"errorCode").toString().toInt();
                transfer.errorMessage = fields.value(u"errorMessage").toString();
            } else {
                // "GID not found": aria2 restarted or the transfer was purged.
                transfer.status = Transfer::Status::Removed;
            }
            transfers.append(transfer);
        }
        done(transfers);
    });
}

} // namespace animehub::downloads
