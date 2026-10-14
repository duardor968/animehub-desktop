#include "app_controller.h"
#include "glyph_provider.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QStyleHints>
#include <QThread>

namespace {

// QML images go through a disk cache: covers and backdrops are immutable, so they load once.
// Only the image loader (off the main thread) gets it; two caches must not share a folder.
class ImageCacheFactory final : public QQmlNetworkAccessManagerFactory {
public:
    explicit ImageCacheFactory(QString directory) : directory_(std::move(directory)) {}

    QNetworkAccessManager* create(QObject* parent) override {
        auto* network = new QNetworkAccessManager(parent);
        if (QThread::currentThread() != qApp->thread()) {
            auto* cache = new QNetworkDiskCache(network);
            cache->setCacheDirectory(directory_);
            cache->setMaximumCacheSize(768LL * 1024 * 1024);
            network->setCache(cache);
        }
        return network;
    }

private:
    QString directory_;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setApplicationName(QStringLiteral("AnimeHub"));
    QApplication::setApplicationVersion(animehub::app::AppController::version());
    QApplication::setOrganizationName(QString());
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/AnimeHub/assets/animehub.png")));
    QApplication::setQuitOnLastWindowClosed(false); // closing can mean "keep running in the tray"

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption data_dir(
        QStringLiteral("data-dir"),
        QStringLiteral("Carpeta para la biblioteca, los ajustes y las cachés."),
        QStringLiteral("carpeta"));
    parser.addOption(data_dir);
    parser.process(app);

    animehub::app::AppPaths paths;
    paths.data = parser.isSet(data_dir)
                     ? QDir(parser.value(data_dir)).absolutePath()
                     : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    paths.engines =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("engines"));
    paths.downloads = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                          .filePath(QStringLiteral("AnimeHub"));

    if (animehub::app::AppController::handOffToRunningInstance(paths.data)) {
        return 0;
    }

    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    animehub::app::AppController controller(paths);
    if (!controller.ready()) {
        QMessageBox::critical(nullptr, QStringLiteral("AnimeHub"), controller.problem());
        return 1;
    }
    controller.listenForOtherInstances(paths.data);

    // Declared before the engine so it outlives it.
    ImageCacheFactory images(QDir(paths.data).filePath(QStringLiteral("cache/images")));
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&images);
    engine.addImageProvider(QStringLiteral("glyph"), new animehub::app::GlyphProvider);
    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&controller)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("AnimeHub", "Main");
    controller.start();
    return QApplication::exec();
}
