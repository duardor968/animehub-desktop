#include "api_json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>

namespace animehub::api::json {
namespace {

// Records the first contract violation; later reads keep returning defaults so a decoder can
// run to the end and report a single, precise path.
class Reader {
public:
    [[nodiscard]] bool failed() const {
        return !error_.isEmpty();
    }
    [[nodiscard]] const QString& error() const {
        return error_;
    }

    void fail(const QString& path, QStringView what) {
        if (error_.isEmpty()) {
            error_ = path + QStringLiteral(": ") + what;
        }
    }

    QJsonValue field(const QJsonObject& object, const QString& path, QLatin1StringView key,
                     bool required) {
        const auto value = object.value(key);
        if (value.isUndefined() && required) {
            fail(join(path, key), u"missing");
        }
        return value;
    }

    QString text(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        const auto value = field(object, path, key, true);
        if (!value.isString()) {
            if (!value.isUndefined()) {
                fail(join(path, key), u"expected string");
            }
            return {};
        }
        return value.toString();
    }

    QString optionalText(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        const auto value = field(object, path, key, false);
        if (value.isUndefined() || value.isNull()) {
            return {};
        }
        if (!value.isString()) {
            fail(join(path, key), u"expected string or null");
            return {};
        }
        return value.toString();
    }

    QUrl optionalUrl(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        const auto raw = optionalText(object, path, key);
        if (raw.isEmpty()) {
            return {};
        }
        QUrl url(raw, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http")) {
            fail(join(path, key), u"expected an http(s) URL");
            return {};
        }
        return url;
    }

    QUrl url(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        if (!object.contains(key) || object.value(key).isNull()) {
            fail(join(path, key), u"missing");
            return {};
        }
        return optionalUrl(object, path, key);
    }

    std::optional<double> optionalNumber(const QJsonObject& object, const QString& path,
                                         QLatin1StringView key) {
        const auto value = field(object, path, key, false);
        if (value.isUndefined() || value.isNull()) {
            return std::nullopt;
        }
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            fail(join(path, key), u"expected number or null");
            return std::nullopt;
        }
        return value.toDouble();
    }

    double number(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        const auto value = field(object, path, key, true);
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            if (!value.isUndefined()) {
                fail(join(path, key), u"expected number");
            }
            return 0;
        }
        return value.toDouble();
    }

    std::optional<int> optionalInteger(const QJsonObject& object, const QString& path,
                                       QLatin1StringView key) {
        const auto value = optionalNumber(object, path, key);
        if (!value) {
            return std::nullopt;
        }
        if (*value != std::trunc(*value) || std::abs(*value) > 2'000'000'000) {
            fail(join(path, key), u"expected integer");
            return std::nullopt;
        }
        return static_cast<int>(*value);
    }

    int integer(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        const auto value = number(object, path, key);
        if (value != std::trunc(value) || std::abs(value) > 2'000'000'000) {
            fail(join(path, key), u"expected integer");
            return 0;
        }
        return static_cast<int>(value);
    }

    bool boolean(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        const auto value = field(object, path, key, true);
        if (!value.isBool()) {
            if (!value.isUndefined()) {
                fail(join(path, key), u"expected boolean");
            }
            return false;
        }
        return value.toBool();
    }

    QJsonObject object(const QJsonObject& parent, const QString& path, QLatin1StringView key) {
        const auto value = field(parent, path, key, true);
        if (!value.isObject()) {
            if (!value.isUndefined()) {
                fail(join(path, key), u"expected object");
            }
            return {};
        }
        return value.toObject();
    }

    QJsonArray array(const QJsonObject& parent, const QString& path, QLatin1StringView key) {
        const auto value = field(parent, path, key, true);
        if (!value.isArray()) {
            if (!value.isUndefined()) {
                fail(join(path, key), u"expected array");
            }
            return {};
        }
        return value.toArray();
    }

    // Each element of `array` must be an object; `decode(object, path)` maps it.
    template <class T, class F>
    QList<T> list(const QJsonArray& array, const QString& path, F&& decode) {
        QList<T> result;
        result.reserve(array.size());
        for (qsizetype index = 0; index < array.size(); ++index) {
            const auto item_path =
                path + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
            if (!array.at(index).isObject()) {
                fail(item_path, u"expected object");
                return {};
            }
            result.append(decode(array.at(index).toObject(), item_path));
        }
        return result;
    }

    QDateTime optionalDateTime(const QJsonObject& object, const QString& path,
                               QLatin1StringView key) {
        const auto raw = optionalText(object, path, key);
        if (raw.isEmpty()) {
            return {};
        }
        auto value = QDateTime::fromString(raw, Qt::ISODateWithMs);
        if (!value.isValid()) {
            value = QDateTime::fromString(raw, Qt::ISODate);
        }
        if (!value.isValid()) {
            fail(join(path, key), u"expected an ISO 8601 date-time");
        }
        return value;
    }

    QDateTime dateTime(const QJsonObject& object, const QString& path, QLatin1StringView key) {
        if (!object.contains(key) || object.value(key).isNull()) {
            fail(join(path, key), u"missing");
            return {};
        }
        return optionalDateTime(object, path, key);
    }

    static QString join(const QString& path, QLatin1StringView key) {
        return path.isEmpty() ? QString(key) : path + QLatin1Char('.') + key;
    }

private:
    QString error_;
};

template <class T> Decoded<T> finish(const Reader& reader, T&& value) {
    if (reader.failed()) {
        return std::unexpected(reader.error());
    }
    return std::forward<T>(value);
}

Decoded<QJsonObject> rootObject(const QByteArray& body) {
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected(QStringLiteral("invalid JSON: ") + error.errorString());
    }
    if (!document.isObject()) {
        return std::unexpected(QStringLiteral("expected a JSON object"));
    }
    return document.object();
}

Category readCategory(Reader& reader, const QJsonObject& object, const QString& path) {
    return Category{
        .id = reader.text(object, path, QLatin1StringView("id")),
        .name = reader.text(object, path, QLatin1StringView("name")),
        .slug = reader.text(object, path, QLatin1StringView("slug")),
    };
}

QList<Category> readCategories(Reader& reader, const QJsonObject& parent, const QString& path,
                               QLatin1StringView key) {
    return reader.list<Category>(reader.array(parent, path, key), Reader::join(path, key),
                                 [&](const QJsonObject& item, const QString& item_path) {
                                     return readCategory(reader, item, item_path);
                                 });
}

void readSummaryInto(Reader& reader, const QJsonObject& object, const QString& path,
                     AnimeSummary& out) {
    using L = QLatin1StringView;
    out.id = reader.text(object, path, L("id"));
    out.slug = reader.text(object, path, L("slug"));
    out.title = reader.text(object, path, L("title"));
    out.synopsis = reader.optionalText(object, path, L("synopsis"));
    out.posterUrl = reader.optionalUrl(object, path, L("posterUrl"));
    out.backdropUrl = reader.optionalUrl(object, path, L("backdropUrl"));
    out.backdropCandidates = parseSrcSet(reader.optionalText(object, path, L("backdropSrcSet")));
    const auto category = object.value(L("category"));
    if (category.isObject()) {
        out.category = readCategory(reader, category.toObject(), Reader::join(path, L("category")));
    } else if (!category.isUndefined() && !category.isNull()) {
        reader.fail(Reader::join(path, L("category")), u"expected object or null");
    }
    out.status = statusFromKey(reader.text(object, path, L("status")));
    out.startDate = reader.optionalDateTime(object, path, L("startDate")).toUTC().date();
    out.mature = reader.boolean(object, path, L("mature"));
    if (!out.slug.isEmpty() && !isSafeSlug(out.slug)) {
        reader.fail(Reader::join(path, L("slug")), u"unexpected characters");
    }
}

AnimeSummary readSummary(Reader& reader, const QJsonObject& object, const QString& path) {
    AnimeSummary summary;
    readSummaryInto(reader, object, path, summary);
    return summary;
}

Episode readEpisode(Reader& reader, const QJsonObject& object, const QString& path) {
    using L = QLatin1StringView;
    Episode episode;
    episode.id = reader.text(object, path, L("id"));
    episode.number = reader.number(object, path, L("number"));
    episode.title = reader.optionalText(object, path, L("title"));
    episode.imageUrl = reader.optionalUrl(object, path, L("imageUrl"));
    episode.publishedAt = reader.optionalDateTime(object, path, L("publishedAt"));
    return episode;
}

QList<AnimeSummary> readSummaries(Reader& reader, const QJsonArray& array, const QString& path) {
    return reader.list<AnimeSummary>(array, path,
                                     [&](const QJsonObject& item, const QString& item_path) {
                                         return readSummary(reader, item, item_path);
                                     });
}

std::optional<RelationKind> relationKindFromKey(QStringView key) {
    static constexpr std::pair<std::u16string_view, RelationKind> kKinds[] = {
        {u"PREQUEL", RelationKind::Prequel},
        {u"SEQUEL", RelationKind::Sequel},
        {u"MAIN_STORY", RelationKind::MainStory},
        {u"FULL_STORY", RelationKind::FullStory},
        {u"SIDE_STORY", RelationKind::SideStory},
        {u"SPIN_OFF", RelationKind::SpinOff},
        {u"SUMMARY", RelationKind::Summary},
        {u"ALTERNATIVE", RelationKind::Alternative},
        {u"ALTERNATIVE_SETTING", RelationKind::AlternativeSetting},
        {u"OTHER", RelationKind::Other},
    };
    for (const auto& [name, kind] : kKinds) {
        if (key == QStringView(name)) {
            return kind;
        }
    }
    return std::nullopt;
}

} // namespace

bool isSafeSlug(QStringView slug) {
    if (slug.isEmpty() || slug.size() > 255) {
        return false;
    }
    for (const auto ch : slug) {
        const bool allowed = (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') ||
                             (ch >= u'0' && ch <= u'9') || ch == u'-' || ch == u'_' || ch == u'.';
        if (!allowed) {
            return false;
        }
    }
    return slug != u"." && slug != u"..";
}

QList<ImageCandidate> parseSrcSet(QStringView srcset) {
    QList<ImageCandidate> candidates;
    for (const auto entry : srcset.split(u',', Qt::SkipEmptyParts)) {
        const auto parts = entry.trimmed().split(u' ', Qt::SkipEmptyParts);
        if (parts.size() != 2 || !parts[1].endsWith(u'w')) {
            continue;
        }
        bool ok = false;
        const int width = parts[1].chopped(1).toInt(&ok);
        const QUrl url(parts[0].toString(), QUrl::StrictMode);
        if (ok && width > 0 && url.isValid() && url.scheme() == u"https") {
            candidates.append({.url = url, .width = width});
        }
    }
    return candidates;
}

Decoded<Home> decodeHome(const QByteArray& body) {
    using L = QLatin1StringView;
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    const auto data = reader.object(*root, {}, L("data"));
    const QString path = QStringLiteral("data");
    Home home;
    home.featured = reader.list<FeaturedAnime>(
        reader.array(data, path, L("featured")), path + QStringLiteral(".featured"),
        [&](const QJsonObject& item, const QString& item_path) {
            FeaturedAnime featured;
            readSummaryInto(reader, item, item_path, featured);
            featured.genres = readCategories(reader, item, item_path, L("genres"));
            featured.episodeCount = reader.optionalInteger(item, item_path, L("episodeCount"));
            featured.trailerUrl = reader.optionalUrl(item, item_path, L("trailerUrl"));
            return featured;
        });
    home.recentEpisodes = reader.list<RecentEpisode>(
        reader.array(data, path, L("recentEpisodes")), path + QStringLiteral(".recentEpisodes"),
        [&](const QJsonObject& item, const QString& item_path) {
            return RecentEpisode{
                .anime = readSummary(reader, reader.object(item, item_path, L("anime")),
                                     item_path + QStringLiteral(".anime")),
                .episode = readEpisode(reader, reader.object(item, item_path, L("episode")),
                                       item_path + QStringLiteral(".episode")),
            };
        });
    home.recentAnime = readSummaries(reader, reader.array(data, path, L("recentAnime")),
                                     path + QStringLiteral(".recentAnime"));
    return finish(reader, std::move(home));
}

Decoded<CatalogPage> decodeCatalog(const QByteArray& body) {
    using L = QLatin1StringView;
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    CatalogPage page;
    page.items = readSummaries(reader, reader.array(*root, {}, L("data")), QStringLiteral("data"));
    const auto meta = reader.object(*root, {}, L("meta"));
    const QString path = QStringLiteral("meta");
    page.page = reader.integer(meta, path, L("page"));
    page.perPage = reader.integer(meta, path, L("perPage"));
    page.totalPages = reader.integer(meta, path, L("totalPages"));
    page.totalRecords = reader.integer(meta, path, L("totalRecords"));
    page.capped = reader.boolean(meta, path, L("capped"));
    page.categories = readCategories(reader, meta, path, L("categories"));
    page.genres = readCategories(reader, meta, path, L("genres"));
    const auto years = reader.array(meta, path, L("years"));
    for (qsizetype index = 0; index < years.size(); ++index) {
        const auto year = years.at(index).toDouble(-1);
        if (!years.at(index).isDouble() || year != std::trunc(year)) {
            reader.fail(path + QStringLiteral(".years[") + QString::number(index) +
                            QLatin1Char(']'),
                        u"expected integer");
            break;
        }
        page.years.append(static_cast<int>(year));
    }
    return finish(reader, std::move(page));
}

Decoded<QList<AnimeSummary>> decodeSuggestions(const QByteArray& body) {
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    auto items = readSummaries(reader, reader.array(*root, {}, QLatin1StringView("data")),
                               QStringLiteral("data"));
    return finish(reader, std::move(items));
}

Decoded<AnimeDetail> decodeAnime(const QByteArray& body) {
    using L = QLatin1StringView;
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    const auto data = reader.object(*root, {}, L("data"));
    const QString path = QStringLiteral("data");
    AnimeDetail detail;
    readSummaryInto(reader, data, path, detail);
    detail.alternativeTitle = reader.optionalText(data, path, L("alternativeTitle"));
    detail.trailerUrl = reader.optionalUrl(data, path, L("trailerUrl"));
    detail.endDate = reader.optionalDateTime(data, path, L("endDate")).toUTC().date();
    detail.nextEpisodeAt = reader.optionalDateTime(data, path, L("nextEpisodeAt"));
    detail.episodeCount = reader.optionalInteger(data, path, L("episodeCount"));
    detail.score = reader.optionalNumber(data, path, L("score"));
    detail.votes = reader.optionalInteger(data, path, L("votes"));
    detail.sourceUrl = reader.url(data, path, L("sourceUrl"));
    detail.genres = readCategories(reader, data, path, L("genres"));
    const auto read_relation = [&](const QJsonObject& item, const QString& item_path) {
        Relation relation;
        const auto kind = relationKindFromKey(reader.text(item, item_path, L("kind")));
        relation.kind = kind.value_or(RelationKind::Other);
        relation.position = reader.integer(item, item_path, L("position"));
        relation.anime = readSummary(reader, reader.object(item, item_path, L("anime")),
                                     item_path + QStringLiteral(".anime"));
        return relation;
    };
    detail.relations = reader.list<Relation>(reader.array(data, path, L("relations")),
                                             path + QStringLiteral(".relations"), read_relation);
    // Old cached responses predate this additive field. A present value still validates strictly.
    if (data.contains(L("incomingRelations"))) {
        detail.incomingRelations =
            reader.list<Relation>(reader.array(data, path, L("incomingRelations")),
                                  path + QStringLiteral(".incomingRelations"), read_relation);
    }
    return finish(reader, std::move(detail));
}

Decoded<EpisodePage> decodeEpisodes(const QByteArray& body) {
    using L = QLatin1StringView;
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    EpisodePage page;
    page.items = reader.list<Episode>(reader.array(*root, {}, L("data")), QStringLiteral("data"),
                                      [&](const QJsonObject& item, const QString& item_path) {
                                          return readEpisode(reader, item, item_path);
                                      });
    const auto meta = reader.object(*root, {}, L("meta"));
    const QString path = QStringLiteral("meta");
    page.page = reader.integer(meta, path, L("page"));
    page.perPage = reader.integer(meta, path, L("perPage"));
    page.totalPages = reader.integer(meta, path, L("totalPages"));
    page.totalRecords = reader.integer(meta, path, L("totalRecords"));
    page.firstNumber = reader.optionalNumber(meta, path, L("firstNumber"));
    page.lastNumber = reader.optionalNumber(meta, path, L("lastNumber"));
    return finish(reader, std::move(page));
}

Decoded<QList<ScheduleEntry>> decodeSchedule(const QByteArray& body) {
    using L = QLatin1StringView;
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    auto entries = reader.list<ScheduleEntry>(
        reader.array(*root, {}, L("data")), QStringLiteral("data"),
        [&](const QJsonObject& item, const QString& item_path) {
            return ScheduleEntry{
                .anime = readSummary(reader, reader.object(item, item_path, L("anime")),
                                     item_path + QStringLiteral(".anime")),
                .latestEpisode =
                    readEpisode(reader, reader.object(item, item_path, L("latestEpisode")),
                                item_path + QStringLiteral(".latestEpisode")),
                .basisPublishedAt = reader.dateTime(item, item_path, L("basisPublishedAt")),
                .isFinalEpisode = reader.boolean(item, item_path, L("isFinalEpisode")),
            };
        });
    return finish(reader, std::move(entries));
}

Decoded<Resolution> decodeResolution(const QByteArray& body) {
    using L = QLatin1StringView;
    const auto root = rootObject(body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Reader reader;
    const auto data = reader.object(*root, {}, L("data"));
    const QString path = QStringLiteral("data");
    Resolution resolution;
    resolution.packageName = reader.text(data, path, L("packageName"));
    resolution.episodes = reader.list<ResolvedEpisode>(
        reader.array(data, path, L("episodes")), path + QStringLiteral(".episodes"),
        [&](const QJsonObject& item, const QString& item_path) {
            ResolvedEpisode episode;
            episode.episodeNumber = reader.number(item, item_path, L("episodeNumber"));
            const auto audio = reader.text(item, item_path, L("audio"));
            if (audio == u"DUB") {
                episode.audio = Audio::Dub;
            } else if (audio != u"SUB" && !reader.failed()) {
                reader.fail(item_path + QStringLiteral(".audio"), u"expected SUB or DUB");
            }
            episode.errorCode = reader.optionalText(item, item_path, L("errorCode"));
            const auto links = reader.array(item, item_path, L("links"));
            for (qsizetype index = 0; index < links.size(); ++index) {
                const auto link_path = item_path + QStringLiteral(".links[") +
                                       QString::number(index) + QLatin1Char(']');
                const auto link = links.at(index).toObject();
                const auto provider = providerFromKey(reader.text(link, link_path, L("provider")));
                const auto url = reader.url(link, link_path, L("url"));
                // A provider added server-side is not an error: this client cannot use it yet.
                if (provider && url.scheme() == u"https") {
                    episode.links.append({.provider = *provider, .url = url});
                }
            }
            return episode;
        });
    return finish(reader, std::move(resolution));
}

std::optional<Problem> decodeProblem(const QByteArray& body) {
    const auto root = rootObject(body);
    if (!root || !root->value(QLatin1StringView("title")).isString()) {
        return std::nullopt;
    }
    return Problem{
        .title = root->value(QLatin1StringView("title")).toString(),
        .detail = root->value(QLatin1StringView("detail")).toString(),
    };
}

QByteArray encodeResolveRequest(const ResolveRequest& request) {
    QJsonArray numbers;
    for (const auto number : request.episodeNumbers) {
        numbers.append(number);
    }
    QJsonArray providers;
    for (const auto provider : request.providers) {
        providers.append(providerKey(provider));
    }
    QJsonObject object{
        {QStringLiteral("episodeNumbers"), numbers},
        {QStringLiteral("audio"),
         request.audio == Audio::Dub ? QStringLiteral("DUB") : QStringLiteral("SUB")},
        {QStringLiteral("providers"), providers},
    };
    if (request.refresh) {
        object.insert(QStringLiteral("refresh"), true);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace animehub::api::json
