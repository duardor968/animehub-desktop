#include "ambient_palette.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <utility>
#include <vector>

namespace animehub::app {

struct AmbientPalette::DispatchState {
    std::mutex mutex;
    QPointer<AmbientPalette> owner;
};

namespace {

constexpr int kSampleWidth = 32;
constexpr int kSampleHeight = 45;
constexpr int kHueBuckets = 12;
constexpr int kMaxCacheEntries = 500;
constexpr int kSaveDebounceMs = 500;
constexpr int kNetworkTimeoutMs = 15000;

constexpr float kNeutralHueDeg = 215.0f;
constexpr float kNeutralSat = 0.14f;
constexpr float kSatMin = 0.25f;
constexpr float kSatMax = 0.43f;
constexpr float kPrimaryValMin = 87.0f / 255.0f;    // >= 0.34 after 8-bit quantization
constexpr float kPrimaryValMax = 117.0f / 255.0f;   // <= 0.46 after 8-bit quantization
constexpr float kSecondaryValMin = 72.0f / 255.0f;  // >= 0.28 after 8-bit quantization
constexpr float kSecondaryValMax = 102.0f / 255.0f; // <= 0.40 after 8-bit quantization
constexpr float kTertiaryValMin = 57.0f / 255.0f;   // >= 0.22 after 8-bit quantization
constexpr float kTertiaryValMax = 86.0f / 255.0f;   // <= 0.34 after 8-bit quantization

struct SharedConfig {
    QPointer<QNetworkAccessManager> network;
    QString cache_file;
    std::vector<QPointer<AmbientPalette>> instances;
};

SharedConfig& sharedConfig() {
    static SharedConfig config;
    return config;
}

AmbientPalette::Palette fallbackPalette() {
    return {
        .primary = QColor(0x27, 0x35, 0x49),
        .secondary = QColor(0x1f, 0x29, 0x3a),
        .tertiary = QColor(0x18, 0x1f, 0x2c),
    };
}

QVariantMap paletteToMap(const AmbientPalette::Palette& palette, bool ready) {
    return {
        {QStringLiteral("primary"), palette.primary.name(QColor::HexRgb)},
        {QStringLiteral("secondary"), palette.secondary.name(QColor::HexRgb)},
        {QStringLiteral("tertiary"), palette.tertiary.name(QColor::HexRgb)},
        {QStringLiteral("ready"), ready},
    };
}

float wrapHue(float degrees) {
    float wrapped = std::fmod(degrees, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped;
}

float hueDistance(float a, float b) {
    const float diff = std::fabs(wrapHue(a) - wrapHue(b));
    return std::min(diff, 360.0f - diff);
}

QColor makeQuantizedHsv(float hue_deg, float sat, float val) {
    const QColor raw = QColor::fromHsvF(wrapHue(hue_deg) / 360.0f, std::clamp(sat, 0.0f, 1.0f),
                                        std::clamp(val, 0.0f, 1.0f));
    return QColor(raw.red(), raw.green(), raw.blue());
}

QColor clampChromaticTone(float hue_deg, float sat, float val, float val_min, float val_max) {
    return makeQuantizedHsv(hue_deg, std::clamp(sat, kSatMin, kSatMax),
                            std::clamp(val, val_min, val_max));
}

AmbientPalette::Palette neutralSlatePalette(float avg_val) {
    const float base = std::clamp(avg_val * 0.55f + 0.14f, kPrimaryValMin, kPrimaryValMax);
    const float mid = std::clamp(base - 0.06f, kSecondaryValMin, kSecondaryValMax);
    const float low = std::clamp(base - 0.12f, kTertiaryValMin, kTertiaryValMax);
    return {
        .primary = makeQuantizedHsv(kNeutralHueDeg, kNeutralSat, base),
        .secondary = makeQuantizedHsv(kNeutralHueDeg + 6.0f, kNeutralSat, mid),
        .tertiary = makeQuantizedHsv(kNeutralHueDeg - 4.0f, kNeutralSat, low),
    };
}

struct HueBucket {
    float weight = 0.0f;
    float cos_sum = 0.0f;
    float sin_sum = 0.0f;
    float sat_sum = 0.0f;
    float val_sum = 0.0f;
};

struct BucketSummary {
    float weight = 0.0f;
    float hue_deg = 0.0f;
    float sat = 0.0f;
    float val = 0.0f;
};

} // namespace

void AmbientPalette::postResult(const std::shared_ptr<DispatchState>& state, const QString& key,
                                const Palette& palette, bool ok) {
    std::lock_guard lock(state->mutex);
    if (!state->owner) {
        return;
    }
    QPointer<AmbientPalette> owner = state->owner;
    QMetaObject::invokeMethod(
        owner.data(),
        [owner, key, palette, ok]() {
            if (owner) {
                owner->finishExtraction(key, palette, ok);
            }
        },
        Qt::QueuedConnection);
}

AmbientPalette::AmbientPalette(QObject* parent)
    : QObject(parent), state_(std::make_shared<DispatchState>()) {
    state_->owner = this;
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(kSaveDebounceMs);
    connect(&save_timer_, &QTimer::timeout, this, &AmbientPalette::saveCache);

    auto& config = sharedConfig();
    config.instances.emplace_back(this);
    network_ = config.network;
    cache_file_ = config.cache_file;
    if (!cache_file_.isEmpty()) {
        loadCache();
    }
}

AmbientPalette::~AmbientPalette() {
    {
        std::lock_guard lock(state_->mutex);
        state_->owner = nullptr;
    }
    auto& instances = sharedConfig().instances;
    std::erase_if(instances, [this](const QPointer<AmbientPalette>& ptr) {
        return ptr.isNull() || ptr.data() == this;
    });
    if (dirty_) {
        save_timer_.stop();
        saveCache();
    }
}

void AmbientPalette::configure(QNetworkAccessManager* network, const QString& cache_file) {
    auto& config = sharedConfig();
    config.network = network;
    config.cache_file = cache_file;
    std::erase_if(config.instances,
                  [](const QPointer<AmbientPalette>& ptr) { return ptr.isNull(); });
    for (const auto& instance : config.instances) {
        if (instance) {
            instance->applyConfiguration(network, cache_file);
        }
    }
}

void AmbientPalette::applyConfiguration(QNetworkAccessManager* network, const QString& cache_file) {
    const bool network_became_ready = network_.isNull() && network != nullptr;
    const bool file_changed = cache_file_ != cache_file;
    network_ = network;
    cache_file_ = cache_file;

    bool loaded_any = false;
    if (file_changed && !cache_file_.isEmpty()) {
        loaded_any = loadCache();
    }
    if (loaded_any || network_became_ready) {
        ++revision_;
        emit revisionChanged();
    }
}

QVariantMap AmbientPalette::colorsFor(const QUrl& posterUrl) {
    if (!posterUrl.isValid() || posterUrl.isEmpty()) {
        return paletteToMap(fallbackPalette(), false);
    }

    const QString key = posterUrl.toString();
    if (const auto it = cache_.constFind(key); it != cache_.constEnd()) {
        return paletteToMap(it.value(), true);
    }

    if (!pending_.contains(key) && !failed_.contains(key)) {
        if (posterUrl.isLocalFile()) {
            const QString file_path = posterUrl.toLocalFile();
            if (!file_path.isEmpty()) {
                pending_.insert(key);
                dispatchExtractFile(key, file_path);
            }
        } else if (posterUrl.scheme() == u"http" || posterUrl.scheme() == u"https") {
            if (network_) {
                pending_.insert(key);
                QNetworkRequest request(posterUrl);
                request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                                     QNetworkRequest::PreferCache);
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);
                request.setTransferTimeout(kNetworkTimeoutMs);
                QNetworkReply* reply = network_->get(request);
                connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
                connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        finishExtraction(key, fallbackPalette(), false);
                        return;
                    }
                    QByteArray data = reply->readAll();
                    if (data.isEmpty()) {
                        finishExtraction(key, fallbackPalette(), false);
                        return;
                    }
                    dispatchExtractBytes(key, std::move(data));
                });
            }
        }
    }

    return paletteToMap(fallbackPalette(), false);
}

AmbientPalette::Palette AmbientPalette::extractFromImage(const QImage& image) {
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return neutralSlatePalette(0.4f);
    }

    QImage scaled =
        image.scaled(kSampleWidth, kSampleHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (scaled.isNull()) {
        return neutralSlatePalette(0.4f);
    }
    if (scaled.format() != QImage::Format_RGBA8888) {
        scaled = std::move(scaled).convertToFormat(QImage::Format_RGBA8888);
    }

    std::array<HueBucket, kHueBuckets> buckets{};
    float chromatic_weight = 0.0f;
    float neutral_weight = 0.0f;
    float neutral_val_sum = 0.0f;

    const float half_w = static_cast<float>(scaled.width()) * 0.5f;
    const float half_h = static_cast<float>(scaled.height()) * 0.5f;
    const float center_x = static_cast<float>(scaled.width() - 1) * 0.5f;
    const float center_y = static_cast<float>(scaled.height() - 1) * 0.5f;
    constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
    constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
    constexpr float kInvSqrt2 = 1.0f / std::numbers::sqrt2_v<float>;

    for (int y = 0; y < scaled.height(); ++y) {
        const uchar* row = scaled.constScanLine(y);
        const float dy = (static_cast<float>(y) - center_y) / half_h;
        for (int x = 0; x < scaled.width(); ++x) {
            const uchar* px = row + (x * 4);
            const uchar alpha = px[3];
            if (alpha < 128) {
                continue;
            }

            const float r = static_cast<float>(px[0]) / 255.0f;
            const float g = static_cast<float>(px[1]) / 255.0f;
            const float b = static_cast<float>(px[2]) / 255.0f;
            const float max_c = std::max({r, g, b});
            const float min_c = std::min({r, g, b});
            const float delta = max_c - min_c;
            const float v = max_c;
            const float s = max_c > 0.0f ? (delta / max_c) : 0.0f;

            if (v < 0.12f || (v > 0.94f && s < 0.15f)) {
                continue;
            }

            const float dx = (static_cast<float>(x) - center_x) / half_w;
            const float dist = std::min(1.0f, std::hypot(dx, dy) * kInvSqrt2);
            const float center_weight = 1.0f - dist;
            const float weight = (0.35f + s) * (0.5f + 0.5f * center_weight);

            if (s < 0.08f || delta <= 0.0f) {
                neutral_weight += weight;
                neutral_val_sum += v * weight;
                continue;
            }

            float hue = 0.0f;
            if (max_c == r) {
                hue = 60.0f * std::fmod((g - b) / delta, 6.0f);
            } else if (max_c == g) {
                hue = 60.0f * (((b - r) / delta) + 2.0f);
            } else {
                hue = 60.0f * (((r - g) / delta) + 4.0f);
            }
            hue = wrapHue(hue);

            const int bucket_index = static_cast<int>(wrapHue(hue + 15.0f) / 30.0f) % kHueBuckets;
            auto& bucket = buckets[static_cast<std::size_t>(bucket_index)];
            const float rad = hue * kDegToRad;
            bucket.weight += weight;
            bucket.cos_sum += std::cos(rad) * weight;
            bucket.sin_sum += std::sin(rad) * weight;
            bucket.sat_sum += s * weight;
            bucket.val_sum += v * weight;
            chromatic_weight += weight;
        }
    }

    const float total_weight = chromatic_weight + neutral_weight;
    if (chromatic_weight <= 0.0f || chromatic_weight < 0.04f * total_weight) {
        const float avg_neutral_val =
            neutral_weight > 0.0f ? (neutral_val_sum / neutral_weight) : 0.4f;
        return neutralSlatePalette(avg_neutral_val);
    }

    std::vector<BucketSummary> ranked;
    ranked.reserve(kHueBuckets);
    for (int i = 0; i < kHueBuckets; ++i) {
        const auto& bucket = buckets[static_cast<std::size_t>(i)];
        if (bucket.weight <= 0.0f) {
            continue;
        }
        float mean_hue = static_cast<float>(i * 30);
        if (std::fabs(bucket.cos_sum) > 1e-6f || std::fabs(bucket.sin_sum) > 1e-6f) {
            mean_hue = wrapHue(std::atan2(bucket.sin_sum, bucket.cos_sum) * kRadToDeg);
        }
        ranked.push_back({
            .weight = bucket.weight,
            .hue_deg = mean_hue,
            .sat = bucket.sat_sum / bucket.weight,
            .val = bucket.val_sum / bucket.weight,
        });
    }

    if (ranked.empty()) {
        return neutralSlatePalette(0.4f);
    }

    std::ranges::sort(
        ranked, [](const BucketSummary& a, const BucketSummary& b) { return a.weight > b.weight; });

    const BucketSummary primary_src = ranked.front();
    BucketSummary secondary_src{
        .weight = 0.0f,
        .hue_deg = wrapHue(primary_src.hue_deg + 28.0f),
        .sat = primary_src.sat * 0.90f,
        .val = primary_src.val * 0.85f,
    };
    for (std::size_t i = 1; i < ranked.size(); ++i) {
        if (ranked[i].weight < primary_src.weight * 0.08f) {
            break;
        }
        if (hueDistance(ranked[i].hue_deg, primary_src.hue_deg) >= 45.0f) {
            secondary_src = ranked[i];
            break;
        }
    }

    BucketSummary tertiary_src{
        .weight = 0.0f,
        .hue_deg = wrapHue(primary_src.hue_deg + 180.0f),
        .sat = primary_src.sat * 0.72f,
        .val = primary_src.val * 0.70f,
    };
    for (std::size_t i = 1; i < ranked.size(); ++i) {
        if (ranked[i].weight < primary_src.weight * 0.05f) {
            break;
        }
        if (hueDistance(ranked[i].hue_deg, primary_src.hue_deg) >= 45.0f &&
            hueDistance(ranked[i].hue_deg, secondary_src.hue_deg) >= 45.0f) {
            tertiary_src = ranked[i];
            break;
        }
    }

    return {
        .primary = clampChromaticTone(primary_src.hue_deg, primary_src.sat, primary_src.val,
                                      kPrimaryValMin, kPrimaryValMax),
        .secondary = clampChromaticTone(secondary_src.hue_deg, secondary_src.sat, secondary_src.val,
                                        kSecondaryValMin, kSecondaryValMax),
        .tertiary = clampChromaticTone(tertiary_src.hue_deg, tertiary_src.sat, tertiary_src.val,
                                       kTertiaryValMin, kTertiaryValMax),
    };
}

void AmbientPalette::dispatchExtractFile(const QString& key, const QString& file_path) {
    const auto state = state_;
    QThreadPool::globalInstance()->start([state, key, file_path]() {
        const QImage image(file_path);
        const bool ok = !image.isNull();
        const Palette palette = ok ? extractFromImage(image) : fallbackPalette();
        postResult(state, key, palette, ok);
    });
}

void AmbientPalette::dispatchExtractBytes(const QString& key, QByteArray data) {
    const auto state = state_;
    QThreadPool::globalInstance()->start([state, key, bytes = std::move(data)]() {
        const QImage image = QImage::fromData(bytes);
        const bool ok = !image.isNull();
        const Palette palette = ok ? extractFromImage(image) : fallbackPalette();
        postResult(state, key, palette, ok);
    });
}

void AmbientPalette::finishExtraction(const QString& key, const Palette& palette, bool ok) {
    pending_.remove(key);
    if (!ok) {
        failed_.insert(key);
        return;
    }
    failed_.remove(key);
    insertCached(key, palette, true);
    ++revision_;
    emit revisionChanged();
}

void AmbientPalette::insertCached(const QString& key, const Palette& palette, bool mark_dirty) {
    const bool already_cached = cache_.contains(key);
    cache_.insert(key, palette);
    if (!already_cached) {
        order_.push_back(key);
        while (cache_.size() > kMaxCacheEntries && !order_.empty()) {
            const QString oldest = std::move(order_.front());
            order_.pop_front();
            cache_.remove(oldest);
        }
    }
    if (mark_dirty && !cache_file_.isEmpty()) {
        dirty_ = true;
        if (!save_timer_.isActive()) {
            save_timer_.start();
        }
    }
}

bool AmbientPalette::loadCache() {
    if (cache_file_.isEmpty()) {
        return false;
    }
    QFile file(cache_file_);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonArray entries = doc.object().value(QStringLiteral("entries")).toArray();
    bool loaded_any = false;
    for (const QJsonValue& value : entries) {
        const QJsonObject obj = value.toObject();
        const QString url = obj.value(QStringLiteral("url")).toString();
        const QJsonArray colors = obj.value(QStringLiteral("colors")).toArray();
        if (url.isEmpty() || colors.size() != 3) {
            continue;
        }
        const QColor primary = QColor::fromString(colors.at(0).toString());
        const QColor secondary = QColor::fromString(colors.at(1).toString());
        const QColor tertiary = QColor::fromString(colors.at(2).toString());
        if (!primary.isValid() || !secondary.isValid() || !tertiary.isValid()) {
            continue;
        }
        if (!cache_.contains(url)) {
            loaded_any = true;
        }
        insertCached(url, {.primary = primary, .secondary = secondary, .tertiary = tertiary},
                     false);
    }
    return loaded_any;
}

void AmbientPalette::saveCache() {
    dirty_ = false;
    if (cache_file_.isEmpty()) {
        return;
    }
    const QFileInfo info(cache_file_);
    QDir().mkpath(info.absolutePath());

    QJsonArray entries;
    for (const QString& key : order_) {
        const auto it = cache_.constFind(key);
        if (it == cache_.constEnd()) {
            continue;
        }
        const Palette& palette = it.value();
        QJsonArray colors;
        colors.append(palette.primary.name(QColor::HexRgb));
        colors.append(palette.secondary.name(QColor::HexRgb));
        colors.append(palette.tertiary.name(QColor::HexRgb));
        QJsonObject entry;
        entry.insert(QStringLiteral("url"), key);
        entry.insert(QStringLiteral("colors"), colors);
        entries.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), entries);

    QSaveFile file(cache_file_);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

} // namespace animehub::app
