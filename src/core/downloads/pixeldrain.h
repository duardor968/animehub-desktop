#ifndef ANIMEHUB_CORE_DOWNLOADS_PIXELDRAIN_H
#define ANIMEHUB_CORE_DOWNLOADS_PIXELDRAIN_H

#include <QObject>
#include <QString>
#include <QUrl>

#include <expected>
#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace animehub::downloads::pixeldrain {

// `https://pixeldrain.com/u/<id>` (also `.net` and `/api/file/<id>`).
[[nodiscard]] std::optional<QString> fileId(const QUrl& url);

struct Info {
    QString name;
    qint64 size = 0;
    QUrl download;
};

struct Failure {
    enum class Kind { NotFound, Unavailable, Network, Protocol };
    Kind kind = Kind::Protocol;
    QString message;
};

class Api final : public QObject {
    Q_OBJECT

public:
    explicit Api(QNetworkAccessManager& network, QObject* parent = nullptr,
                 QUrl base = QUrl(QStringLiteral("https://pixeldrain.com/")));

    void info(const QString& id, QObject* context,
              std::function<void(const std::expected<Info, Failure>&)> done);

private:
    QNetworkAccessManager& network_;
    QUrl base_;
};

} // namespace animehub::downloads::pixeldrain

#endif // ANIMEHUB_CORE_DOWNLOADS_PIXELDRAIN_H
