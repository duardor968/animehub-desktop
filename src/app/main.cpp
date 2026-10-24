#include "app_controller.h"
#include "glyph_provider.h"
#include "smoke_flow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <thread>
#ifdef Q_OS_WIN
#include <Windows.h>
#include <crtdbg.h>
#endif

namespace {

// A headless failure must never raise a Windows/CRT dialog on the user's desktop.
// This runs before QApplication because platform and rendering initialization can fail there.
void suppressHeadlessErrorDialogs() {
    qputenv("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES", "1");
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (const auto report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
#endif
#endif
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        const auto text = message.toLocal8Bit();
        std::fprintf(stderr, "%s\n", text.constData());
        std::fflush(stderr);
    });
}

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
    std::jthread smokeWatchdog;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--smoke-screenshot" || argument.starts_with("--smoke-screenshot=") ||
            argument == "--smoke-hidden-native") {
            suppressHeadlessErrorDialogs();
            smokeWatchdog = std::jthread([](std::stop_token stop) {
                std::mutex mutex;
                std::condition_variable_any wake;
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, std::chrono::seconds(20), [] { return false; });
                if (!stop.stop_requested()) {
                    // Do not acquire a logger/Qt lock if a rendering thread is stuck.
                    std::_Exit(4);
                }
            });
            break;
        }
    }
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == u"offscreen" &&
        qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR",
                QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")).toUtf8());
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
    const QCommandLineOption api_base(QStringLiteral("api-base-url"),
                                      QStringLiteral("URL base de la API, incluido /api/v1."),
                                      QStringLiteral("url"));
    const QCommandLineOption smoke_image(
        QStringLiteral("smoke-screenshot"),
        QStringLiteral("Captura offscreen y termina; requiere --data-dir."),
        QStringLiteral("archivo"));
    const QCommandLineOption smoke_size(QStringLiteral("smoke-size"),
                                        QStringLiteral("Tamaño de la captura WxH."),
                                        QStringLiteral("tamaño"), QStringLiteral("1280x820"));
    const QCommandLineOption smoke_page(
        QStringLiteral("smoke-page"),
        QStringLiteral("Página: library, catalog, downloads, settings o linked-devices."),
        QStringLiteral("página"), QStringLiteral("library"));
    const QCommandLineOption smoke_view(
        QStringLiteral("smoke-library-view"),
        QStringLiteral("Lista de la biblioteca para la captura: grouped (por franquicia) o flat."),
        QStringLiteral("vista"), QStringLiteral("grouped"));
    const QCommandLineOption smoke_archive(QStringLiteral("smoke-archive"),
                                           QStringLiteral("Capturar el Archivo de la biblioteca."));
    const QCommandLineOption smoke_franchise(
        QStringLiteral("smoke-franchise"), QStringLiteral("Franquicia del perfil que se captura."),
        QStringLiteral("id"));
    const QCommandLineOption smoke_anime(QStringLiteral("smoke-anime"),
                                         QStringLiteral("Anime del perfil que se captura."),
                                         QStringLiteral("slug"));
    const QCommandLineOption profile_transfers(
        QStringLiteral("enable-profile-transfers"),
        QStringLiteral("Permitir transferencias en un perfil de prueba limpio."));
    const QCommandLineOption smoke_hidden_native(
        QStringLiteral("smoke-hidden-native"),
        QStringLiteral("Capturar con una ventana nativa creada siempre oculta; requiere smoke."));
    const QCommandLineOption smoke_flow(QStringLiteral("smoke-flow"),
                                        QStringLiteral("Recorrido Qt: create, archive o settings."),
                                        QStringLiteral("caso"));
    parser.addOption(smoke_flow);
    parser.addOptions({api_base, smoke_image, smoke_size, smoke_page, profile_transfers,
                       smoke_hidden_native, smoke_view, smoke_archive, smoke_franchise,
                       smoke_anime});
    // How Windows starts it at sign-in: in the tray, without the window.
    const QCommandLineOption tray(QStringLiteral("tray"),
                                  QStringLiteral("Empezar en la bandeja, sin abrir la ventana."));
    parser.addOption(tray);
    parser.process(app);
    const bool smoke = parser.isSet(smoke_image);
    const bool smokeHidden = parser.isSet(smoke_hidden_native);
    if (parser.isSet(smoke_flow) &&
        (!smokeHidden || !QStringList{QStringLiteral("create"), QStringLiteral("archive"),
                                      QStringLiteral("settings")}
                              .contains(parser.value(smoke_flow))))
        return 2;
    if (smokeHidden &&
        (!smoke || !parser.isSet(data_dir) || QGuiApplication::platformName() != u"windows")) {
        qCritical("Smoke nativo oculto requiere captura, perfil y plataforma windows");
        return 2;
    }
    if (parser.isSet(data_dir) && !QDir::isAbsolutePath(parser.value(data_dir))) {
        qCritical("--data-dir debe ser absoluto");
        return 2;
    }
    if (smoke && (!parser.isSet(data_dir) ||
                  (!smokeHidden && QGuiApplication::platformName() != u"offscreen"))) {
        qCritical("Smoke requiere --data-dir y QT_QPA_PLATFORM=offscreen");
        return 2;
    }
    const auto sizeMatch = QRegularExpression(QStringLiteral("^([0-9]{3,4})x([0-9]{3,4})$"))
                               .match(parser.value(smoke_size));
    const QStringList smokePages{QStringLiteral("library"), QStringLiteral("catalog"),
                                 QStringLiteral("downloads"), QStringLiteral("settings"),
                                 QStringLiteral("linked-devices")};
    if (smoke && (!sizeMatch.hasMatch() || !smokePages.contains(parser.value(smoke_page))))
        return 2;
    const QStringList smokeViews{QStringLiteral("grouped"), QStringLiteral("flat")};
    const bool librarySmokeOptions = parser.isSet(smoke_view) || parser.isSet(smoke_archive) ||
                                     parser.isSet(smoke_franchise) || parser.isSet(smoke_anime);
    bool franchiseIdValid = false;
    const qint64 smokeFranchiseId = parser.value(smoke_franchise).toLongLong(&franchiseIdValid);
    if ((librarySmokeOptions && (!smoke || !parser.value(smoke_page).startsWith(u"library"))) ||
        !smokeViews.contains(parser.value(smoke_view)) ||
        (parser.isSet(smoke_franchise) && (!franchiseIdValid || smokeFranchiseId <= 0)) ||
        (parser.isSet(smoke_anime) && parser.value(smoke_anime).trimmed().isEmpty()) ||
        (parser.isSet(smoke_franchise) && parser.isSet(smoke_anime)))
        return 2;
    if (smoke) {
        const auto logPath = parser.value(smoke_image) + QStringLiteral(".log");
        QDir().mkpath(QFileInfo(logPath).absolutePath());
        app.setProperty("smokeLogPath", logPath);
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
            QFile log(QCoreApplication::instance()->property("smokeLogPath").toString());
            if (log.open(QIODevice::WriteOnly | QIODevice::Append))
                log.write(QByteArray::number(type) + ": " + text.toUtf8() + '\n');
        });
    }
    const bool hidden = parser.isSet(tray) && QSystemTrayIcon::isSystemTrayAvailable();

    animehub::app::AppPaths paths;
    paths.data = parser.isSet(data_dir)
                     ? QDir(parser.value(data_dir)).absolutePath()
                     : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    paths.engines =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("engines"));
    paths.downloads = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                          .filePath(QStringLiteral("AnimeHub"));
    paths.isolated = parser.isSet(data_dir);
    const QFileInfo defaultProfile(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    const QFileInfo selectedProfile(paths.data);
    const auto defaultCanonical = defaultProfile.canonicalFilePath();
    const auto selectedCanonical = selectedProfile.canonicalFilePath();
    if (paths.isolated &&
        (QDir(paths.data) == QDir(defaultProfile.absoluteFilePath()) ||
         (!defaultCanonical.isEmpty() && !selectedCanonical.isEmpty() &&
          defaultCanonical.compare(selectedCanonical, Qt::CaseInsensitive) == 0))) {
        qCritical("El perfil explícito no puede ser la biblioteca predeterminada");
        return 2;
    }
    if (paths.isolated && !selectedCanonical.isEmpty())
        paths.data = selectedCanonical;
    paths.smoke = smoke;
    paths.transfers = !smoke && (!paths.isolated || parser.isSet(profile_transfers));
    if (paths.isolated)
        paths.downloads = QDir(paths.data).filePath(QStringLiteral("downloads"));
    if (parser.isSet(api_base)) {
        paths.apiBase = QUrl(parser.value(api_base));
        if (!paths.apiBase.isValid() || paths.apiBase.host().isEmpty() ||
            (paths.apiBase.scheme() != u"http" && paths.apiBase.scheme() != u"https"))
            return 2;
    }

    if (animehub::app::AppController::handOffToRunningInstance(paths.data, !hidden && !smoke)) {
        return smoke ? 6 : 0;
    }

    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    animehub::app::AppController controller(paths);
    if (!controller.ready()) {
        if (smoke) {
            qCritical("%s", qPrintable(controller.problem()));
            return 1;
        }
        QMessageBox::critical(nullptr, QStringLiteral("AnimeHub"), controller.problem());
        return 1;
    }
    controller.listenForOtherInstances(paths.data);

    // Declared before the engine so it outlives it.
    ImageCacheFactory images(QDir(paths.data).filePath(QStringLiteral("cache/images")));
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&images);
    engine.addImageProvider(QStringLiteral("glyph"), new animehub::app::GlyphProvider);
    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&controller)},
                                 {QStringLiteral("smokeHidden"), smokeHidden},
                                 {QStringLiteral("visible"), !hidden && !smokeHidden}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("AnimeHub", "Main");
    controller.start();
    if (smoke) {
        if (engine.rootObjects().isEmpty())
            return 1;
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window)
            return 1;
        if (!smokeHidden)
            window->setVisibility(QWindow::Windowed);
        window->resize(sizeMatch.captured(1).toInt(), sizeMatch.captured(2).toInt());
        if (smokeHidden) {
            if (window->isVisible())
                return 7;
            window->create(); // Creates a hidden HWND; never calls show or activates it.
        }
        QMetaObject::invokeMethod(
            window, "showPage",
            Q_ARG(QVariant, parser.value(smoke_page) == u"downloads" ? QVariant(2)
                            : parser.value(smoke_page) == u"catalog" ? QVariant(1)
                                                                     : QVariant(0)));
        if (parser.value(smoke_page).startsWith(u"library")) {
            if (parser.isSet(smoke_view))
                controller.library()->setGrouped(parser.value(smoke_view) == u"grouped");
            controller.library()->setArchivedOnly(parser.isSet(smoke_archive));
            auto* page = window->findChild<QObject*>(QStringLiteral("libraryPage"));
            if (!page)
                return 5;
            if (parser.isSet(smoke_franchise)) {
                page->setProperty("selected", QString());
                page->setProperty("selectedGroup", smokeFranchiseId);
                controller.library()->openFranchise(smokeFranchiseId);
                if (controller.library()->franchise().isEmpty())
                    return 5;
            } else if (parser.isSet(smoke_anime)) {
                if (!controller.library()->contains(parser.value(smoke_anime)))
                    return 5;
                QMetaObject::invokeMethod(page, "select",
                                          Q_ARG(QVariant, parser.value(smoke_anime)));
            }
        }
        if (parser.value(smoke_page) == u"settings" ||
            parser.value(smoke_page) == u"linked-devices") {
            const auto openDialog = [window, smokeHidden](const QString& name) {
                auto* dialog = window->findChild<QObject*>(name);
                if (!dialog)
                    return false;
                // Qt 6.11 Fluent Dialog inherits Popup.Item (enum value 0). Refuse a
                // separate/native popup rather than altering the product's style for QA.
                const auto popupType = dialog->property("popupType");
                if (smokeHidden && (!popupType.isValid() || popupType.toInt() != 0))
                    return false;
                return QMetaObject::invokeMethod(dialog, "open");
            };
            if (!openDialog(QStringLiteral("settingsDialog")))
                return 5;
            if (parser.value(smoke_page) == u"linked-devices") {
                QTimer::singleShot(100, &app, [openDialog] {
                    if (!openDialog(QStringLiteral("remoteDevices")))
                        QCoreApplication::exit(5);
                });
            }
        }
        if (parser.isSet(smoke_flow)) {
            QTimer::singleShot(650, &app, [window, &controller, flow = parser.value(smoke_flow)] {
                if (!animehub::app::runSmokeFlow(window, controller.library(), flow))
                    QCoreApplication::exit(8);
            });
        }
        // A hidden window has no regular render loop. Render a few frames so asynchronous
        // images and the product's own fade-in transitions settle before the evidence frame.
        auto* warmup = new QTimer(&app);
        warmup->setInterval(100);
        QObject::connect(warmup, &QTimer::timeout, window, [window, smokeHidden] {
            if (smokeHidden && window->isVisible()) {
                QCoreApplication::exit(7);
                return;
            }
            window->grabWindow();
        });
        warmup->start();
        QTimer::singleShot(
            2500, &app, [window, warmup, smokeHidden, file = parser.value(smoke_image)] {
                warmup->stop();
                if (auto* grid = window->findChild<QObject*>(QStringLiteral("franchiseWorks"))) {
                    qInfo("Smoke franchise grid: height=%.1f contentY=%.1f originY=%.1f",
                          grid->property("height").toDouble(),
                          grid->property("contentY").toDouble(),
                          grid->property("originY").toDouble());
                    if (auto* header = grid->property("headerItem").value<QObject*>())
                        qInfo("Smoke franchise header: y=%.1f height=%.1f",
                              header->property("y").toDouble(),
                              header->property("height").toDouble());
                }
                if (smokeHidden && window->isVisible()) {
                    QCoreApplication::exit(7);
                    return;
                }
                QDir().mkpath(QFileInfo(file).absolutePath());
                const auto image = window->grabWindow();
                if (image.isNull() || !image.save(file)) {
                    qCritical("Smoke: captura vacía o no guardada");
                    QCoreApplication::exit(3);
                    return;
                }
                qInfo("Smoke: %dx%d guardada", image.width(), image.height());
                QCoreApplication::exit(0);
            });
        QTimer::singleShot(15000, &app, [] { QCoreApplication::exit(4); });
    }
    return QApplication::exec();
}
