#ifndef ANIMEHUB_APP_AMBIENT_PALETTE_H
#define ANIMEHUB_APP_AMBIENT_PALETTE_H

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <deque>
#include <memory>

class QNetworkAccessManager;

namespace animehub::app {

// Extracts dark-safe dominant ambient colours from a poster once off the UI thread, caches them
// in memory and on disk, and notifies QML bindings through `revision` when new colours arrive.
class AmbientPalette : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    struct Palette {
        QColor primary;
        QColor secondary;
        QColor tertiary;

        bool operator==(const Palette&) const = default;
    };

    explicit AmbientPalette(QObject* parent = nullptr);
    ~AmbientPalette() override;

    [[nodiscard]] int revision() const {
        return revision_;
    }

    Q_INVOKABLE QVariantMap colorsFor(const QUrl& posterUrl);

    [[nodiscard]] static Palette extractFromImage(const QImage& image);
    static void configure(QNetworkAccessManager* network, const QString& cache_file);

signals:
    void revisionChanged();

private:
    struct DispatchState;

    static void postResult(const std::shared_ptr<DispatchState>& state, const QString& key,
                           const Palette& palette, bool ok);
    void applyConfiguration(QNetworkAccessManager* network, const QString& cache_file);
    void dispatchExtractFile(const QString& key, const QString& file_path);
    void dispatchExtractBytes(const QString& key, QByteArray data);
    void finishExtraction(const QString& key, const Palette& palette, bool ok);
    void insertCached(const QString& key, const Palette& palette, bool mark_dirty);
    bool loadCache();
    void saveCache();

    std::shared_ptr<DispatchState> state_;
    QPointer<QNetworkAccessManager> network_;
    QString cache_file_;
    QHash<QString, Palette> cache_;
    std::deque<QString> order_;
    QSet<QString> pending_;
    QSet<QString> failed_;
    QTimer save_timer_;
    int revision_ = 0;
    bool dirty_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_AMBIENT_PALETTE_H
