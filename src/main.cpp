#include "app_info.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>

#include <cstdlib>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const auto application_name =
        QString::fromLatin1(animehub::kApplicationName.data(),
                            static_cast<qsizetype>(animehub::kApplicationName.size()));
    const auto application_version =
        QString::fromLatin1(animehub::kApplicationVersion.data(),
                            static_cast<qsizetype>(animehub::kApplicationVersion.size()));

    QCoreApplication::setApplicationName(application_name);
    QCoreApplication::setApplicationVersion(application_version);
    QCoreApplication::setOrganizationName(application_name);

    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("AnimeHub", "Main");

    return QGuiApplication::exec();
}
