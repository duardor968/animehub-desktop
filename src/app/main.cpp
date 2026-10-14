#include <QApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadFromModule("AnimeHub", "Main");
    return QApplication::exec();
}
