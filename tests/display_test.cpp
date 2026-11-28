#include "app/display.h"
#include "app/schedule.h"

#include <QTest>
#include <QTimeZone>

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

    // While AnimeHub waits for the JDownloader it started, the episode says so.
    void aHandOverWaitingForJDownloaderSaysItOpens() {
        downloads::Task task;
        task.state = downloads::TaskState::Resolving;
        QCOMPARE(display::download(task), QStringLiteral("Buscando el archivo…"));
        task.engine = downloads::DownloadEngine::JDownloaderLocal;
        task.enginePhase = QStringLiteral("launching");
        QCOMPARE(display::download(task), QStringLiteral("Abriendo JDownloader…"));
    }

    // The agenda's day headings and lines, by the same rule.
    void theAgendaUsesTheSameDays() {
        const QDateTime now(QDate(2026, 10, 31), QTime(9, 0));
        const app::schedule::Next tuesday{.number = 5,
                                          .at = QDateTime(QDate(2026, 11, 3), QTime(10, 36))};
        QCOMPARE(app::schedule::day(tuesday, now), QStringLiteral("Martes"));
        QCOMPARE(app::schedule::slot(tuesday, now), QStringLiteral("10:36 · Ep. 5"));
        QCOMPARE(app::schedule::badge(tuesday, now), QString());
        QCOMPARE(app::schedule::timeText(tuesday, now), QStringLiteral("10:36"));
        const app::schedule::Next later{.number = 7,
                                        .at = QDateTime(QDate(2026, 11, 7), QTime(8, 41))};
        QCOMPARE(app::schedule::day(later, now), QStringLiteral("7 nov"));
        // Late stays where it is, under "Hoy", and says so; one due on an earlier day keeps
        // that day beside its hour.
        const app::schedule::Next late{.number = 8, .at = now.addSecs(-5 * 3600), .late = true};
        QCOMPARE(app::schedule::day(late, now), QStringLiteral("Hoy"));
        QCOMPARE(app::schedule::slot(late, now), QStringLiteral("04:00 · Ep. 8 · Retrasado"));
        QCOMPARE(app::schedule::state(late, now), QStringLiteral("late"));
        QCOMPARE(app::schedule::badge(late, now), QStringLiteral("Retrasado"));
        QCOMPARE(app::schedule::timeText(late, now), QStringLiteral("04:00"));
        const app::schedule::Next overnight{
            .number = 8, .at = now.addSecs(-30 * 3600), .late = true};
        QCOMPARE(app::schedule::day(overnight, now), QStringLiteral("Hoy"));
        QCOMPARE(app::schedule::slot(overnight, now),
                 QStringLiteral("30 oct, 03:00 · Ep. 8 · Retrasado"));
        QCOMPARE(app::schedule::timeText(overnight, now), QStringLiteral("Vie 03:00"));
        // Still to come today, or due within the grace: "Próximo", as on AnimeHub Web.
        const app::schedule::Next tonight{.number = 9, .at = now.addSecs(12 * 3600)};
        QCOMPARE(app::schedule::day(tonight, now), QStringLiteral("Hoy"));
        QCOMPARE(app::schedule::slot(tonight, now), QStringLiteral("21:00 · Ep. 9 · Próximo"));
        QCOMPARE(app::schedule::state(tonight, now), QStringLiteral("upcoming"));
        QCOMPARE(app::schedule::badge(tonight, now), QStringLiteral("Próximo"));
        QCOMPARE(app::schedule::timeText(tonight, now), QStringLiteral("21:00"));
        const app::schedule::Next due{.number = 9, .at = now.addSecs(-3600)};
        QCOMPARE(app::schedule::slot(due, now), QStringLiteral("08:00 · Ep. 9 · Próximo"));
        QCOMPARE(app::schedule::state(due, now), QStringLiteral("upcoming"));
        QCOMPARE(app::schedule::badge(due, now), QStringLiteral("Próximo"));
        QCOMPARE(app::schedule::state(tuesday, now), QString());
    }

    // A slot whose episode came out today stays today, as aired, instead of reading as a date a
    // week on; the agenda never reaches a seventh day.
    void theAgendaShowsWhatAiredToday() {
        const QDateTime now(QDate(2026, 11, 25), QTime(15, 0));
        api::ScheduleEntry slot;
        slot.anime.status = api::AnimeStatus::Airing;
        slot.latestEpisode.number = 9;
        slot.basisPublishedAt = QDateTime(QDate(2026, 11, 25), QTime(9, 37));
        QCOMPARE(app::schedule::next(slot, now)->at.date(), QDate(2026, 12, 2));
        const auto today = app::schedule::agenda(slot, now);
        QVERIFY(today && today->aired);
        QCOMPARE(today->number, 9.0);
        QCOMPARE(app::schedule::day(*today, now), QStringLiteral("Hoy"));
        QCOMPARE(app::schedule::slot(*today, now), QStringLiteral("09:37 · Ep. 9 · Emitido"));
        QCOMPARE(app::schedule::state(*today, now), QStringLiteral("aired"));
        QCOMPARE(app::schedule::badge(*today, now), QStringLiteral("Emitido"));
        QCOMPARE(app::schedule::timeText(*today, now), QStringLiteral("09:37"));

        // A finale out today uses "Final" / "finale"; the day after, the slot is gone.
        slot.isFinalEpisode = true;
        slot.anime.status = api::AnimeStatus::Finished;
        const auto finale = app::schedule::agenda(slot, now);
        QVERIFY(finale && finale->aired && finale->finale);
        QCOMPARE(app::schedule::slot(*finale, now), QStringLiteral("09:37 · Ep. 9 · Final"));
        QCOMPARE(app::schedule::state(*finale, now), QStringLiteral("finale"));
        QCOMPARE(app::schedule::badge(*finale, now), QStringLiteral("Final"));
        QCOMPARE(app::schedule::timeText(*finale, now), QStringLiteral("09:37"));
        QVERIFY(!app::schedule::agenda(slot, now.addDays(1)));

        // Out yesterday: next week's, six days on at most.
        slot.isFinalEpisode = false;
        slot.anime.status = api::AnimeStatus::Airing;
        slot.basisPublishedAt = QDateTime(QDate(2026, 11, 24), QTime(23, 50));
        const auto coming = app::schedule::agenda(slot, now);
        QVERIFY(coming && !coming->aired);
        QCOMPARE(coming->number, 10.0);
        QCOMPARE(app::schedule::day(*coming, now), QStringLiteral("Martes"));
    }

    // [at, at + 3h) stays "Próximo" / "upcoming" (never "Ahora"), switching to "Retrasado" /
    // "late" at exactly +3h.
    void graceWindowTransitionsAtThreeHours() {
        api::ScheduleEntry entry;
        entry.anime.status = api::AnimeStatus::Airing;
        entry.latestEpisode.number = 4;
        entry.basisPublishedAt = QDateTime(QDate(2026, 11, 18), QTime(12, 0));
        const QDateTime expected_at(QDate(2026, 11, 25), QTime(12, 0));

        const auto at_release = app::schedule::agenda(entry, expected_at);
        QVERIFY(at_release && !at_release->late);
        QCOMPARE(app::schedule::state(*at_release, expected_at), QStringLiteral("upcoming"));
        QCOMPARE(app::schedule::badge(*at_release, expected_at), QStringLiteral("Próximo"));
        QCOMPARE(app::schedule::slot(*at_release, expected_at),
                 QStringLiteral("12:00 · Ep. 5 · Próximo"));

        const auto end_of_grace = expected_at.addSecs(3 * 3600 - 1);
        const auto within_grace = app::schedule::agenda(entry, end_of_grace);
        QVERIFY(within_grace && !within_grace->late);
        QCOMPARE(app::schedule::state(*within_grace, end_of_grace), QStringLiteral("upcoming"));
        QCOMPARE(app::schedule::badge(*within_grace, end_of_grace), QStringLiteral("Próximo"));

        const auto at_three_hours = expected_at.addSecs(3 * 3600);
        const auto late = app::schedule::agenda(entry, at_three_hours);
        QVERIFY(late && late->late);
        QCOMPARE(app::schedule::state(*late, at_three_hours), QStringLiteral("late"));
        QCOMPARE(app::schedule::badge(*late, at_three_hours), QStringLiteral("Retrasado"));
        QCOMPARE(app::schedule::slot(*late, at_three_hours),
                 QStringLiteral("12:00 · Ep. 5 · Retrasado"));
    }

    // Weekly projection uses addDays(7) so DST spring-forward and fall-back preserve local hour.
    void weeklyProjectionKeepsLocalHourAcrossDst() {
        const QTimeZone madrid(QByteArrayLiteral("Europe/Madrid"));
        QVERIFY(madrid.isValid());

        api::ScheduleEntry spring;
        spring.anime.status = api::AnimeStatus::Airing;
        spring.latestEpisode.number = 1;
        spring.basisPublishedAt = QDateTime(QDate(2026, 3, 28), QTime(15, 30), madrid);
        const auto spring_next = app::schedule::next(spring, spring.basisPublishedAt.addDays(2));
        QVERIFY(spring_next);
        QCOMPARE(spring_next->at.date(), QDate(2026, 4, 4));
        QCOMPARE(spring_next->at.time(), QTime(15, 30));
        QCOMPARE(spring.basisPublishedAt.secsTo(spring_next->at), 7LL * 86400 - 3600);

        api::ScheduleEntry fall;
        fall.anime.status = api::AnimeStatus::Airing;
        fall.latestEpisode.number = 1;
        fall.basisPublishedAt = QDateTime(QDate(2026, 10, 24), QTime(15, 30), madrid);
        const auto fall_next = app::schedule::next(fall, fall.basisPublishedAt.addDays(2));
        QVERIFY(fall_next);
        QCOMPARE(fall_next->at.date(), QDate(2026, 10, 31));
        QCOMPARE(fall_next->at.time(), QTime(15, 30));
        QCOMPARE(fall.basisPublishedAt.secsTo(fall_next->at), 7LL * 86400 + 3600);
    }

    void bytesFormattingUsesSpanishDecimalsForSmallMegabytesAndGigabytes() {
        QCOMPARE(display::bytes(0), QStringLiteral("0 KB"));
        QCOMPARE(display::bytes(500'000), QStringLiteral("500 KB"));
        QCOMPARE(display::bytes(1'100'000), QStringLiteral("1,1 MB"));
        QCOMPARE(display::bytes(1'400'000), QStringLiteral("1,4 MB"));
        QCOMPARE(display::bytes(10'000'000), QStringLiteral("10 MB"));
        QCOMPARE(display::bytes(286'000'000), QStringLiteral("286 MB"));
        QCOMPARE(display::bytes(2'000'000'000), QStringLiteral("2,0 GB"));
    }

    void remainingDurationAndSpanishPlurals() {
        QCOMPARE(display::remaining(0), QStringLiteral("menos de un minuto"));
        QCOMPARE(display::remaining(59), QStringLiteral("menos de un minuto"));
        QCOMPARE(display::remaining(60), QStringLiteral("1 min"));
        QCOMPARE(display::remaining(61), QStringLiteral("2 min"));
        QCOMPARE(display::remaining(3599), QStringLiteral("1 h"));
        QCOMPARE(display::remaining(3600), QStringLiteral("1 h"));
        QCOMPARE(display::remaining(3660), QStringLiteral("1 h 1 min"));
        QCOMPARE(display::remaining(7200), QStringLiteral("2 h"));
        QCOMPARE(display::remaining(7320), QStringLiteral("2 h 2 min"));

        QCOMPARE(display::remainingPhrase(30), QStringLiteral("queda menos de un minuto"));
        QCOMPARE(display::remainingPhrase(30, true), QStringLiteral("Queda menos de un minuto"));
        QCOMPARE(display::remainingPhrase(60), QStringLiteral("queda 1 min"));
        QCOMPARE(display::remainingPhrase(60, true), QStringLiteral("Queda 1 min"));
        QCOMPARE(display::remainingPhrase(120), QStringLiteral("quedan 2 min"));
        QCOMPARE(display::remainingPhrase(120, true), QStringLiteral("Quedan 2 min"));
        QCOMPARE(display::remainingPhrase(3600), QStringLiteral("queda 1 h"));
        QCOMPARE(display::remainingPhrase(3600, true), QStringLiteral("Queda 1 h"));
        QCOMPARE(display::remainingPhrase(3660), QStringLiteral("queda 1 h 1 min"));
        QCOMPARE(display::remainingPhrase(7200), QStringLiteral("quedan 2 h"));
        QCOMPARE(display::remainingPhrase(7200, true), QStringLiteral("Quedan 2 h"));

        QCOMPARE(display::shortRemaining(0), QString());
        QCOMPARE(display::shortRemaining(1), QStringLiteral("1 s"));
        QCOMPARE(display::shortRemaining(45), QStringLiteral("45 s"));
        QCOMPARE(display::shortRemaining(60), QStringLiteral("1 min"));
        QCOMPARE(display::shortRemaining(3600), QStringLiteral("1 h"));
        QCOMPARE(display::shortRemaining(7200), QStringLiteral("2 h"));

        QCOMPARE(display::shortRemainingPhrase(0), QString());
        QCOMPARE(display::shortRemainingPhrase(1), QStringLiteral("queda 1 s"));
        QCOMPARE(display::shortRemainingPhrase(2), QStringLiteral("quedan 2 s"));
        QCOMPARE(display::shortRemainingPhrase(60), QStringLiteral("queda 1 min"));
        QCOMPARE(display::shortRemainingPhrase(120), QStringLiteral("quedan 2 min"));
        QCOMPARE(display::shortRemainingPhrase(3600), QStringLiteral("queda 1 h"));
        QCOMPARE(display::shortRemainingPhrase(7200), QStringLiteral("quedan 2 h"));

        downloads::Task downloading;
        downloading.state = downloads::TaskState::Downloading;
        downloading.bytesDone = 0;
        downloading.bytesTotal = 30'000'000;
        downloading.bytesPerSecond = 1'000'000;
        QVERIFY(
            display::download(downloading).contains(QStringLiteral("queda menos de un minuto")));
        downloading.bytesTotal = 60'000'000;
        QVERIFY(display::download(downloading).contains(QStringLiteral("queda 1 min")));
        downloading.bytesTotal = 3'600'000'000;
        QVERIFY(display::download(downloading).contains(QStringLiteral("queda 1 h")));
        QVERIFY(!display::download(downloading).contains(QStringLiteral("0 min")));

        downloads::Task cancelled;
        cancelled.state = downloads::TaskState::Cancelled;
        QCOMPARE(display::download(cancelled), QStringLiteral("Cancelado"));
    }

    void storageAndSpeedLimitErrorsTranslateRawEnglishAndKeepSpanish() {
        QCOMPARE(
            display::storageError(
                QStringLiteral("the library was written by a newer AnimeHub (schema 9)")),
            QStringLiteral("La biblioteca fue creada por una versión más reciente de AnimeHub."));
        QCOMPARE(display::storageError(QStringLiteral("database is locked")),
                 QStringLiteral("La biblioteca está ocupada. Vuelve a intentarlo."));
        QCOMPARE(display::storageError(QStringLiteral("database or disk is full")),
                 QStringLiteral("No queda espacio en el disco."));
        QCOMPARE(
            display::storageError(QStringLiteral("attempt to write a readonly database")),
            QStringLiteral(
                "No se pudo escribir en la biblioteca. Comprueba los permisos de la carpeta."));
        QCOMPARE(display::storageError(QStringLiteral("database disk image is malformed")),
                 QStringLiteral("El archivo de la biblioteca está dañado."));
        QCOMPARE(display::storageError(QStringLiteral("unknown personal anime")),
                 QStringLiteral("Este anime ya no está en la biblioteca."));
        QCOMPARE(display::storageError(QStringLiteral("empty franchise title")),
                 QStringLiteral("El nombre de la franquicia no puede estar vacío."));
        const auto generic =
            display::storageError(QStringLiteral("SQLITE_ERROR: near syntax error"));
        QVERIFY(!generic.contains(QStringLiteral("SQLITE"), Qt::CaseInsensitive));
        QVERIFY(!generic.contains(QStringLiteral("syntax"), Qt::CaseInsensitive));

        const auto spanish = QStringLiteral(
            "El archivo Frieren.mkv contiene otros episodios fuera de la selección.");
        QCOMPARE(display::storageError(spanish), spanish);

        const auto conn_err = display::speedLimitError(QStringLiteral("Connection refused"));
        QVERIFY(!conn_err.contains(QStringLiteral("Connection"), Qt::CaseInsensitive));
        QVERIFY(conn_err.contains(QStringLiteral("no respondió")));
        const auto rpc_err = display::speedLimitError(QStringLiteral("Invalid option value"));
        QVERIFY(!rpc_err.contains(QStringLiteral("Invalid"), Qt::CaseInsensitive));
        QVERIFY(rpc_err.contains(QStringLiteral("límite de velocidad")));
    }
};

QTEST_GUILESS_MAIN(DisplayTest)
#include "display_test.moc"
