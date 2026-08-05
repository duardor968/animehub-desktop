#include "app_info.h"

#include <QTest>

class AppInfoTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesProductIdentity() const {
        QCOMPARE(animehub::kApplicationName, std::string_view{"AnimeHub"});
        QCOMPARE(animehub::kApplicationVersion, std::string_view{"0.1.0"});
    }
};

QTEST_GUILESS_MAIN(AppInfoTest)

#include "app_info_test.moc"
