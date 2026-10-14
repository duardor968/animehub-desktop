#include "api/api_json.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace animehub::api;

namespace {

QByteArray fixture(const char* name) {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/") + QLatin1StringView(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", name);
    }
    return file.readAll();
}

template <class T> QString why(const json::Decoded<T>& decoded) {
    return decoded ? QString() : decoded.error();
}

// Re-serialises `body` after `edit` mutates its root object.
template <class F> QByteArray edited(const QByteArray& body, F&& edit) {
    auto root = QJsonDocument::fromJson(body).object();
    edit(root);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class ApiJsonTest final : public QObject {
    Q_OBJECT

private slots:
    void decodesRealResponses() {
        const auto home = json::decodeHome(fixture("home.json"));
        QVERIFY2(home, qPrintable(why(home)));
        QVERIFY(!home->featured.isEmpty());
        QVERIFY(!home->recentEpisodes.isEmpty());
        QVERIFY(!home->recentAnime.isEmpty());

        const auto catalog = json::decodeCatalog(fixture("catalog.json"));
        QVERIFY2(catalog, qPrintable(why(catalog)));
        QCOMPARE(catalog->page, 1);
        QVERIFY(catalog->totalPages > 1);
        QVERIFY(!catalog->genres.isEmpty());
        QVERIFY(!catalog->years.isEmpty());

        const auto suggestions = json::decodeSuggestions(fixture("suggestions.json"));
        QVERIFY2(suggestions, qPrintable(why(suggestions)));

        const auto schedule = json::decodeSchedule(fixture("schedule.json"));
        QVERIFY2(schedule, qPrintable(why(schedule)));
        QVERIFY(!schedule->isEmpty());
        QVERIFY(schedule->first().basisPublishedAt.isValid());
    }

    void decodesAnimeDetail() {
        const auto anime = json::decodeAnime(fixture("anime.json"));
        QVERIFY2(anime, qPrintable(why(anime)));
        QCOMPARE(anime->slug, QStringLiteral("sousou-no-frieren"));
        QCOMPARE(anime->status, AnimeStatus::Finished);
        QCOMPARE(anime->episodeCount, 28);
        QCOMPARE(anime->startDate, QDate(2023, 9, 29));
        QCOMPARE(anime->genres.size(), 4);
        QVERIFY(anime->category.has_value());
        QCOMPARE(anime->backdropCandidates.size(), 3);
        QVERIFY(anime->backdropFor(1000).toString().contains(u"-1280.webp"));
        QVERIFY(anime->backdropFor(5000).toString().contains(u"-1900.webp"));
        QVERIFY(!anime->relations.isEmpty());
        QCOMPARE(anime->relations.first().kind, RelationKind::Sequel);
    }

    void decodesEpisodePage() {
        const auto page = json::decodeEpisodes(fixture("episodes.json"));
        QVERIFY2(page, qPrintable(why(page)));
        QCOMPARE(page->page, 2);
        QCOMPARE(page->perPage, 50);
        QCOMPARE(page->items.size(), 50);
        QCOMPARE(page->items.first().number, 51.0);
        QCOMPARE(page->lastNumber, 1180.0);
    }

    void reportsThePathOfAContractViolation() {
        const auto missing = edited(fixture("anime.json"), [](QJsonObject& root) {
            auto data = root[u"data"].toObject();
            data.remove(u"title");
            root[u"data"] = data;
        });
        const auto anime = json::decodeAnime(missing);
        QVERIFY(!anime);
        QCOMPARE(anime.error(), QStringLiteral("data.title: missing"));

        const auto wrong_type = edited(fixture("episodes.json"), [](QJsonObject& root) {
            auto meta = root[u"meta"].toObject();
            meta[u"page"] = QStringLiteral("2");
            root[u"meta"] = meta;
        });
        const auto page = json::decodeEpisodes(wrong_type);
        QVERIFY(!page);
        QCOMPARE(page.error(), QStringLiteral("meta.page: expected number"));

        QVERIFY(!json::decodeHome("not json"));
        QVERIFY(!json::decodeHome("[]"));
    }

    void rejectsUnsafeSlugsAndUrls() {
        const auto traversal = edited(fixture("anime.json"), [](QJsonObject& root) {
            auto data = root[u"data"].toObject();
            data[u"slug"] = QStringLiteral("../etc");
            root[u"data"] = data;
        });
        QVERIFY(!json::decodeAnime(traversal));

        const auto script_url = edited(fixture("anime.json"), [](QJsonObject& root) {
            auto data = root[u"data"].toObject();
            data[u"posterUrl"] = QStringLiteral("javascript:alert(1)");
            root[u"data"] = data;
        });
        QVERIFY(!json::decodeAnime(script_url));

        QVERIFY(json::isSafeSlug(u"Castlevania"));
        QVERIFY(json::isSafeSlug(u"re-zero-kara-hajimeru-isekai-seikatsu-3rd-season"));
        QVERIFY(!json::isSafeSlug(u".."));
        QVERIFY(!json::isSafeSlug(u"a/b"));
        QVERIFY(!json::isSafeSlug(u""));
    }

    void skipsProvidersThisClientDoesNotKnow() {
        const auto resolution = json::decodeResolution(fixture("resolution.json"));
        QVERIFY2(resolution, qPrintable(why(resolution)));
        QCOMPARE(resolution->episodes.size(), 2);
        QCOMPARE(resolution->episodes[0].links.size(), 1);
        QCOMPARE(resolution->episodes[0].links[0].provider, Provider::Mega);
        QCOMPARE(resolution->episodes[1].episodeNumber, 2.5);
        QCOMPARE(resolution->episodes[1].audio, Audio::Dub);
        QCOMPARE(resolution->episodes[1].errorCode, QStringLiteral("NO_LINKS"));
    }

    void decodesProblemDetails() {
        const auto problem = json::decodeProblem(fixture("notfound.json"));
        QVERIFY(problem);
        QCOMPARE(problem->detail, QStringLiteral("Anime not found."));
        QVERIFY(!json::decodeProblem("{\"data\":1}"));
    }

    void encodesResolveRequests() {
        const auto body =
            json::encodeResolveRequest({.episodeNumbers = {1, 2.5},
                                        .audio = Audio::Sub,
                                        .providers = {Provider::Mega, Provider::Pixeldrain}});
        const auto object = QJsonDocument::fromJson(body).object();
        QCOMPARE(object[u"audio"].toString(), QStringLiteral("SUB"));
        QCOMPARE(object[u"episodeNumbers"].toArray().at(1).toDouble(), 2.5);
        QCOMPARE(object[u"providers"].toArray().at(1).toString(), QStringLiteral("PIXELDRAIN"));
        QVERIFY(!object.contains(u"refresh"));
    }

    void parsesSrcSet() {
        const auto candidates =
            json::parseSrcSet(u"https://a.test/640.webp 640w, https://a.test/1280.webp 1280w,"
                              u" bad, http://insecure.test/x.webp 99w");
        QCOMPARE(candidates.size(), 2);
        QCOMPARE(candidates[1].width, 1280);
    }
};

QTEST_GUILESS_MAIN(ApiJsonTest)
#include "api_json_test.moc"
