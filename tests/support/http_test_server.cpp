#include "http_test_server.h"

#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace animehub::testing {
namespace {

QByteArray reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 206:
        return "Partial Content";
    case 404:
        return "Not Found";
    case 416:
        return "Range Not Satisfiable";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 509:
        return "Bandwidth Limit Exceeded";
    default:
        return "Status";
    }
}

} // namespace

HttpTestServer::HttpTestServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpTestServer::accept);
}

bool HttpTestServer::listen() {
    return server_.listen(QHostAddress::LocalHost, 0);
}

QUrl HttpTestServer::url(const QString& path) const {
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(server_.serverPort());
    const auto query_at = path.indexOf(QLatin1Char('?'));
    url.setPath(query_at < 0 ? path : path.left(query_at));
    if (query_at >= 0) {
        url.setQuery(path.mid(query_at + 1));
    }
    return url;
}

void HttpTestServer::route(const QByteArray& method, const QByteArray& path, Handler handler) {
    routes_.insert(method + ' ' + path, std::move(handler));
}

void HttpTestServer::json(const QByteArray& path, const QByteArray& body, int status) {
    route("GET", path, [body, status](const Request&) {
        return Response{
            .status = status, .body = body, .headers = {{"Content-Type", "application/json"}}};
    });
}

void HttpTestServer::file(const QByteArray& path, const QByteArray& data, int bytes_per_tick,
                          qsizetype stall_after) {
    route("GET", path, [data, bytes_per_tick, stall_after](const Request& request) {
        static const QRegularExpression kRange(QStringLiteral("^bytes=(\\d+)-(\\d*)$"));
        const auto range = request.headers.value("range");
        if (range.isEmpty()) {
            return Response{.status = 200,
                            .body = data,
                            .headers = {{"Content-Type", "application/octet-stream"},
                                        {"Accept-Ranges", "bytes"}},
                            .bytesPerTick = bytes_per_tick,
                            .stallAfter = stall_after};
        }
        const auto match = kRange.match(QString::fromLatin1(range));
        const qint64 first = match.hasMatch() ? match.captured(1).toLongLong() : -1;
        qint64 last = match.hasMatch() && !match.captured(2).isEmpty()
                          ? match.captured(2).toLongLong()
                          : data.size() - 1;
        if (first < 0 || first >= data.size() || last < first) {
            return Response{
                .status = 416,
                .headers = {{"Content-Range", "bytes */" + QByteArray::number(data.size())}}};
        }
        last = std::min<qint64>(last, data.size() - 1);
        return Response{.status = 206,
                        .body = data.mid(first, last - first + 1),
                        .headers = {{"Content-Type", "application/octet-stream"},
                                    {"Accept-Ranges", "bytes"},
                                    {"Content-Range", "bytes " + QByteArray::number(first) + '-' +
                                                          QByteArray::number(last) + '/' +
                                                          QByteArray::number(data.size())}},
                        .bytesPerTick = bytes_per_tick,
                        .stallAfter = stall_after};
    });
}

int HttpTestServer::count(const QByteArray& path) const {
    return static_cast<int>(std::ranges::count_if(
        requests_, [&](const Request& request) { return request.path == path; }));
}

void HttpTestServer::accept() {
    while (auto* socket = server_.nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const auto header_end = buffer->indexOf("\r\n\r\n");
            if (header_end < 0) {
                return;
            }
            Request request;
            const auto lines = buffer->left(header_end).split('\n');
            const auto start = lines.value(0).trimmed().split(' ');
            request.method = start.value(0);
            const auto target = start.value(1);
            const auto query_at = target.indexOf('?');
            request.path = query_at < 0 ? target : target.left(query_at);
            request.query = query_at < 0 ? QByteArray() : target.mid(query_at + 1);
            for (qsizetype index = 1; index < lines.size(); ++index) {
                const auto colon = lines[index].indexOf(':');
                if (colon > 0) {
                    request.headers.insert(lines[index].left(colon).trimmed().toLower(),
                                           lines[index].mid(colon + 1).trimmed());
                }
            }
            const auto length = request.headers.value("content-length", "0").toLongLong();
            if (buffer->size() < header_end + 4 + length) {
                return;
            }
            request.body = buffer->mid(header_end + 4, length);
            buffer->clear();
            requests_.append(request);
            answer(socket, request);
        });
    }
}

void HttpTestServer::answer(QTcpSocket* socket, const Request& request) {
    const auto handler = routes_.value(request.method + ' ' + request.path);
    const auto response =
        handler ? handler(request) : Response{.status = 404, .body = "{\"title\":\"Not Found\"}"};
    QTimer::singleShot(response.delayMs, socket, [socket, response] {
        if (response.drop) {
            socket->abort();
            return;
        }
        QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' +
                          reason(response.status) + "\r\n";
        for (const auto& [name, value] : response.headers) {
            head += name + ": " + value + "\r\n";
        }
        head += "Content-Length: " + QByteArray::number(response.body.size()) +
                "\r\nConnection: close\r\n\r\n";
        if (response.bytesPerTick <= 0) {
            socket->write(head + response.body);
            socket->disconnectFromHost();
            return;
        }
        socket->write(head);
        auto* timer = new QTimer(socket);
        auto offset = std::make_shared<qsizetype>(0);
        QObject::connect(timer, &QTimer::timeout, socket, [socket, timer, response, offset] {
            if (socket->state() != QAbstractSocket::ConnectedState) {
                timer->stop();
                return;
            }
            if (response.stallAfter >= 0 && *offset >= response.stallAfter) {
                timer->stop(); // keep the connection open, send nothing more
                return;
            }
            socket->write(response.body.mid(*offset, response.bytesPerTick));
            *offset += response.bytesPerTick;
            if (*offset >= response.body.size()) {
                timer->stop();
                socket->disconnectFromHost();
            }
        });
        timer->start(10);
    });
}

} // namespace animehub::testing
