#include "app/display.h"
#include "app/schedule.h"

#include <QTest>

using namespace animehub;
namespace display = app::display;

class DisplayTest final : public QObject {
    Q_OBJECT

private slots:
    // One way of saying a day across the app: today and tomorrow by name, the weekday within the
    // coming week, the date from then on; always as the start of a line.
    void daysReadOneWay_data() {
        QTest::addColumn<QDate>("day");
        QTest::addColumn<QString>("expected");
        const QDate today(2026, 10, 31); // a Saturday
        QTest::newRow("today") << today << QStringLiteral("Hoy");
        QTest::newRow("tomorrow") << today.addDays(1) << QStringLiteral("Mañana");
        QTest::newRow("this tuesday") << today.addDays(3) << QStringLiteral("Martes");
        QTest::newRow("this wednesday") << today.addDays(4) << QStringLiteral("Miércoles");
        QTest::newRow("six days on") << today.addDays(6) << QStringLiteral("Viernes");
        // The same weekday a week on would read as this one's.
        QTest::newRow("a week on") << today.addDays(7) << QStringLiteral("7 nov");
        QTest::newRow("next year") << QDate(2027, 1, 12) << QStringLiteral("12 ene 2027");
        QTest::newRow("gone by") << today.addDays(-18) << QStringLiteral("13 oct");
    }
    void daysReadOneWay() {
        QFETCH(QDate, day);
        QFETCH(QString, expected);
        QCOMPARE(display::day(day, QDate(2026, 10, 31)), expected);
    }

    // The time on the 24-hour clock after the day; after a date, set apart.
    void aDayAndItsTime() {
        const QDateTime now(QDate(2026, 10, 31), QTime(9, 0));
        QCOMPARE(display::when(QDateTime(QDate(2026, 11, 3), QTime(11, 42)), now),
                 QStringLiteral("Martes 11:42"));
        QCOMPARE(display::when(QDateTime(QDate(2026, 10, 31), QTime(22, 0)), now),
                 QStringLiteral("Hoy 22:00"));
        QCOMPARE(display::when(QDateTime(QDate(2026, 11, 7), QTime(8, 5)), now),
                 QStringLiteral("7 nov, 08:05"));
        QCOMPARE(display::date(QDateTime(QDate(2026, 10, 13), QTime(12, 0)), QDate(2026, 10, 31)),
                 QStringLiteral("13 oct"));
    }

    // The agenda's day headings and lines, by the same rule.
    void theAgendaUsesTheSameDays() {
        const QDateTime now(QDate(2026, 10, 31), QTime(9, 0));
        const app::schedule::Next tuesday{.number = 5,
                                          .at = QDateTime(QDate(2026, 11, 3), QTime(10, 36))};
        QCOMPARE(app::schedule::day(tuesday, now), QStringLiteral("Martes"));
        QCOMPARE(app::schedule::slot(tuesday, now), QStringLiteral("10:36 · Ep. 5"));
        const app::schedule::Next later{.number = 7,
                                        .at = QDateTime(QDate(2026, 11, 7), QTime(8, 41))};
        QCOMPARE(app::schedule::day(later, now), QStringLiteral("7 nov"));
        const app::schedule::Next late{.number = 8, .at = now.addSecs(-5 * 3600), .late = true};
        QCOMPARE(app::schedule::day(late, now), QStringLiteral("Retrasados"));
    }
};

QTEST_GUILESS_MAIN(DisplayTest)
#include "display_test.moc"
