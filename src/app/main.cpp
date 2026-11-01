#include "app_controller.h"
#include "glyph_provider.h"
#include "smoke_flow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
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
#include <QTcpServer>
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
            // A hidden window has no vsync to pace the threaded render loop: its frames could
            // stall in grabWindow() and its animations stand still between captures. The basic
            // loop renders each grab on this thread, with the animations at the wall clock.
            if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP"))
                qputenv("QSG_RENDER_LOOP", "basic");
            smokeWatchdog = std::jthread([](std::stop_token stop) {
                std::mutex mutex;
                std::condition_variable_any wake;
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, std::chrono::seconds(30), [] { return false; });
                if (!stop.stop_requested()) {
                    // Do not acquire a logger/Qt lock if a rendering thread is stuck.
                    std::_Exit(4);
                }
            });
            break;
        }
    }
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == u"offscreen" &&
        qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR",
                QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")).toUtf8());
#endif
    QApplication::setApplicationName(QStringLiteral("AnimeHub"));
    QApplication::setApplicationVersion(animehub::app::AppController::version());
    QApplication::setOrganizationName(QString());
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/AnimeHub/assets/animehub.png")));
    QApplication::setQuitOnLastWindowClosed(false); // closing can mean "keep running in the tray"
#ifdef Q_OS_LINUX
    // The desktop finds AnimeHub's name and icon in the desktop entry of this name.
    QGuiApplication::setDesktopFileName(QStringLiteral(ANIMEHUB_APP_ID));
    // Theme.iconFont: Segoe Fluent Icons is only on Windows.
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/uno-fluentui-assets.ttf"));
#endif

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
    const QCommandLineOption smoke_settings(
        QStringLiteral("smoke-settings-page"),
        QStringLiteral("Categoría de Ajustes: downloads, general, linked-devices o about "
                       "(playback abre general y jdownloader, downloads)."),
        QStringLiteral("categoría"), QStringLiteral("downloads"));
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
                                        QStringLiteral("Recorrido Qt: create, archive, settings, "
                                                       "navigate o catalog."),
                                        QStringLiteral("caso"));
    parser.addOption(smoke_flow);
    const QCommandLineOption smoke_dialog(
        QStringLiteral("smoke-dialog"),
        QStringLiteral("Diálogo de JDownloader: offer-jdownloader, offer-servers, setup-install, "
                       "setup-local, setup-account, setup-test o setup-test-error."),
        QStringLiteral("diálogo"));
    parser.addOption(smoke_dialog);
    // Animations end at the hidden window's own pace: a busy machine needs longer.
    const QCommandLineOption smoke_wait(QStringLiteral("smoke-wait"),
                                        QStringLiteral("Milisegundos antes de la captura."),
                                        QStringLiteral("ms"), QStringLiteral("2500"));
    parser.addOption(smoke_wait);
    const QCommandLineOption smoke_unfold(
        QStringLiteral("smoke-unfold"),
        QStringLiteral("Desplegar los episodios de la serie a la vista en Descargas."));
    parser.addOption(smoke_unfold);
    const QCommandLineOption smoke_frames(
        QStringLiteral("smoke-frames"),
        QStringLiteral("Capturas sucesivas, 350 ms entre ellas (archivo-2.png, archivo-3.png…)."),
        QStringLiteral("n"), QStringLiteral("1"));
    parser.addOption(smoke_frames);
    parser.addOptions({api_base, smoke_image, smoke_size, smoke_page, smoke_settings,
                       profile_transfers, smoke_hidden_native, smoke_view, smoke_archive,
                       smoke_franchise, smoke_anime});
    // How Windows starts it at sign-in: in the tray, without the window.
    const QCommandLineOption tray(QStringLiteral("tray"),
                                  QStringLiteral("Empezar en la bandeja, sin abrir la ventana."));
    parser.addOption(tray);
    parser.process(app);
    const bool smoke = parser.isSet(smoke_image);
    const bool smokeHidden = parser.isSet(smoke_hidden_native);
    if (parser.isSet(smoke_flow) &&
        (!smokeHidden ||
         !QStringList{QStringLiteral("create"), QStringLiteral("archive"),
                      QStringLiteral("settings"), QStringLiteral("navigate"),
                      QStringLiteral("catalog"), QStringLiteral("offer"), QStringLiteral("setup")}
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
    const QStringList smokeSettings{QStringLiteral("downloads"), QStringLiteral("general"),
                                    QStringLiteral("linked-devices"), QStringLiteral("about")};
    if (parser.isSet(smoke_settings) && (!smoke || parser.value(smoke_page) != u"settings" ||
                                         !smokeSettings.contains(parser.value(smoke_settings))))
        return 2;
    const QStringList smokeDialogs{
        QStringLiteral("offer-jdownloader"), QStringLiteral("offer-servers"),
        QStringLiteral("setup-install"),     QStringLiteral("setup-local"),
        QStringLiteral("setup-account"),     QStringLiteral("setup-test"),
        QStringLiteral("setup-test-error")};
    if (parser.isSet(smoke_dialog) &&
        (!smoke || !smokeDialogs.contains(parser.value(smoke_dialog))))
        return 2;
    bool waitValid = false;
    const int smokeWait = parser.value(smoke_wait).toInt(&waitValid);
    if ((parser.isSet(smoke_wait) || parser.isSet(smoke_unfold)) && !smoke)
        return 2;
    if (!waitValid || smokeWait < 500 || smokeWait > 12000)
        return 2;
    bool framesValid = false;
    const int smokeFrames = parser.value(smoke_frames).toInt(&framesValid);
    if (!framesValid || smokeFrames < 1 || smokeFrames > 6 ||
        (parser.isSet(smoke_frames) && !smoke))
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
    paths.detectJDownloader = parser.value(smoke_dialog) != u"setup-install";
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
                QMetaObject::invokeMethod(
                    page, "select", Q_ARG(QVariant, parser.value(smoke_anime)),
                    Q_ARG(QVariant, QStringLiteral("library")), Q_ARG(QVariant, false));
            }
        }
        // Ajustes on one of its categories; linked-devices is Ajustes on that category.
        if (parser.value(smoke_page) == u"settings" ||
            parser.value(smoke_page) == u"linked-devices") {
            auto* dialog = window->findChild<QObject*>(QStringLiteral("settingsDialog"));
            if (!dialog)
                return 5;
            // Qt 6.11 Fluent Dialog inherits Popup.Item (enum value 0). Refuse a
            // separate/native popup rather than altering the product's style for QA.
            const auto popupType = dialog->property("popupType");
            if (smokeHidden && (!popupType.isValid() || popupType.toInt() != 0))
                return 5;
            const auto category = parser.value(smoke_page) == u"linked-devices"
                                      ? QStringLiteral("linked-devices")
                                      : parser.value(smoke_settings);
            QVariant opened;
            if (!QMetaObject::invokeMethod(dialog, "openAt", Q_RETURN_ARG(QVariant, opened),
                                           Q_ARG(QVariant, category)) ||
                !opened.toBool())
                return 5;
        }
        // The series on view unfolded into its episodes, once the queue is on the page.
        if (parser.isSet(smoke_unfold)) {
            QTimer::singleShot(1000, &app, [window, &controller] {
                if (auto* page = window->findChild<QObject*>(QStringLiteral("downloadsPage")))
                    QMetaObject::invokeMethod(
                        page, "toggle", Q_ARG(QVariant, controller.downloads()->headerSlug()));
            });
        }
        // JDownloader's offer and setup, without a download or a connection behind them,
        // opened once the page has settled (--smoke-wait).
        const auto dialogName = parser.value(smoke_dialog);
        auto* setup = window->findChild<QObject*>(QStringLiteral("jdSetup"));
        auto* offer = window->findChild<QObject*>(QStringLiteral("downloadOffer"));
        if (!dialogName.isEmpty() && (!setup || !offer))
            return 5;
        // "setup-test" shows the test running: its local API is a port that takes the
        // connection and never answers. "setup-test-error" uses the profile's, a closed port.
        auto* silent = new QTcpServer(&app);
        if (dialogName == u"setup-test") {
            if (!silent->listen(QHostAddress::LocalHost))
                return 5;
            controller.settings()->setJdEndpoint(
                QStringLiteral("http://127.0.0.1:%1").arg(silent->serverPort()));
        }
        auto* dialog = dialogName.isEmpty()               ? nullptr
                       : dialogName.startsWith(u"offer-") ? offer
                                                          : setup;
        QTimer::singleShot(smokeWait, &app, [&controller, setup, dialogName] {
            if (dialogName.startsWith(u"offer-")) {
                using animehub::api::Provider;
                controller.downloads()->previewOffer(
                    dialogName == u"offer-servers"
                        ? animehub::app::DownloadsView::Offer::Servers
                        : animehub::app::DownloadsView::Offer::JDownloader,
                    QStringLiteral("Tougen Anki: Nikko Kegon no Taki-hen"), 5,
                    {Provider::OneFichier, Provider::Mp4Upload});
            } else if (dialogName.startsWith(u"setup-")) {
                const int step = dialogName == u"setup-install"         ? 0
                                 : dialogName.startsWith(u"setup-test") ? 2
                                                                        : 1;
                QMetaObject::invokeMethod(setup, "begin", Q_ARG(QVariant, step),
                                          Q_ARG(QVariant, dialogName == u"setup-account"));
            }
        });
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
        const auto capture = [window, warmup, smokeHidden, file = parser.value(smoke_image),
                              frames = smokeFrames, &app] {
            warmup->stop();
            if (auto* grid = window->findChild<QObject*>(QStringLiteral("franchiseWorks"))) {
                qInfo("Smoke franchise grid: height=%.1f contentY=%.1f originY=%.1f",
                      grid->property("height").toDouble(), grid->property("contentY").toDouble(),
                      grid->property("originY").toDouble());
                if (auto* header = grid->property("headerItem").value<QObject*>())
                    qInfo("Smoke franchise header: y=%.1f height=%.1f",
                          header->property("y").toDouble(), header->property("height").toDouble());
            }
            if (smokeHidden && window->isVisible()) {
                QCoreApplication::exit(7);
                return;
            }
            QDir().mkpath(QFileInfo(file).absolutePath());
            // Successive frames show what moves; the warm-up keeps frames coming between them.
            auto* shot = new QTimer(&app);
            shot->setInterval(350);
            auto taken = std::make_shared<int>(0);
            const auto take = [window, warmup, shot, taken, file, frames] {
                warmup->stop();
                const QFileInfo info(file);
                const auto path =
                    *taken == 0 ? file
                                : info.absolutePath() + u'/' + info.completeBaseName() + u'-' +
                                      QString::number(*taken + 1) + u'.' + info.suffix();
                const auto image = window->grabWindow();
                if (image.isNull() || !image.save(path)) {
                    qCritical("Smoke: captura vacía o no guardada");
                    QCoreApplication::exit(3);
                    return;
                }
                qInfo("Smoke: %dx%d guardada", image.width(), image.height());
                if (++*taken >= frames) {
                    shot->stop();
                    QCoreApplication::exit(0);
                    return;
                }
                warmup->start();
            };
            QObject::connect(shot, &QTimer::timeout, window, take);
            take();
            if (*taken < frames)
                shot->start();
        };
        // A flow runs while frames keep rendering, and the capture shows where it ended.
        if (parser.isSet(smoke_flow)) {
            QTimer::singleShot(650, &app,
                               [window, &controller, capture, flow = parser.value(smoke_flow)] {
                                   if (animehub::app::runSmokeFlow(window, controller.library(),
                                                                   flow, controller.downloads()))
                                       capture();
                                   else
                                       QCoreApplication::exit(8);
                               });
        } else if (dialog) {
            // Not at a fixed time: once the dialog is open, its transition over, and the state
            // it is captured in reached.
            auto* ready = new QTimer(&app);
            ready->setInterval(100);
            auto openedFor = std::make_shared<int>(0);
            QObject::connect(ready, &QTimer::timeout, &app,
                             [ready, dialog, openedFor, capture, &controller, dialogName] {
                                 const auto* jd = controller.jdownloader();
                                 const bool state = dialogName == u"setup-test" ? jd->testing()
                                                    : dialogName == u"setup-test-error"
                                                        ? !jd->testProblem().isEmpty()
                                                        : true;
                                 *openedFor = dialog->property("opened").toBool() && state
                                                  ? *openedFor + 1
                                                  : 0;
                                 if (*openedFor < 5) // half a second open and settled
                                     return;
                                 ready->stop();
                                 capture();
                             });
            ready->start();
        } else {
            QTimer::singleShot(smokeWait, &app, capture);
        }
        QTimer::singleShot(25000, &app, [] { QCoreApplication::exit(4); });
    }
    return QApplication::exec();
}
