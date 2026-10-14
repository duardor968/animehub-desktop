#ifndef ANIMEHUB_TESTS_HTTP_TEST_SERVER_H
#define ANIMEHUB_TESTS_HTTP_TEST_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QUrl>

#include <functional>

namespace animehub::testing {

// Minimal HTTP/1.1 server on 127.0.0.1 for tests: one request per connection, routes by method
// and path, byte ranges for file resources.
class HttpTestServer final : public QObject {
    Q_OBJECT

public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QByteArray query;
        QHash<QByteArray, QByteArray> headers; // lower-case names
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray body;
        QList<std::pair<QByteArray, QByteArray>> headers;
        int delayMs = 0;
        bool drop = false;         // close the socket without answering
        int bytesPerTick = 0;      // > 0 sends the body in slices every 10 ms (slow transfers)
        qsizetype stallAfter = -1; // with bytesPerTick: stop sending there, connection open
    };

    using Handler = std::function<Response(const Request&)>;

    explicit HttpTestServer(QObject* parent = nullptr);

    [[nodiscard]] bool listen();
    [[nodiscard]] QUrl url(const QString& path = QStringLiteral("/")) const;

    void route(const QByteArray& method, const QByteArray& path, Handler handler);
    void json(const QByteArray& path, const QByteArray& body, int status = 200);
    // Serves `data` honouring `Range: bytes=a-b`; `bytes_per_tick` slows the transfer down.
    void file(const QByteArray& path, const QByteArray& data, int bytes_per_tick = 0,
              qsizetype stall_after = -1);

    [[nodiscard]] const QList<Request>& requests() const {
        return requests_;
    }
    [[nodiscard]] int count(const QByteArray& path) const;

private:
    void accept();
    void answer(class QTcpSocket* socket, const Request& request);

    QTcpServer server_;
    QHash<QByteArray, Handler> routes_;
    QList<Request> requests_;
};

} // namespace animehub::testing

#endif // ANIMEHUB_TESTS_HTTP_TEST_SERVER_H
