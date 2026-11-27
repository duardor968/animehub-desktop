#include "app/ambient_palette.h"
#include "http_test_server.h"

#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <cmath>

using animehub::app::AmbientPalette;
using animehub::testing::HttpTestServer;

namespace {

float circularHueDistance(float a, float b) {
    const float diff = std::fabs(a - b);
    return std::min(diff, 360.0f - diff);
}

void verifyDarkSafeChromaticBounds(const AmbientPalette::Palette& palette) {
    QVERIFY(palette.primary.isValid());
    QVERIFY(palette.secondary.isValid());
    QVERIFY(palette.tertiary.isValid());

    for (const QColor& tone : {palette.primary, palette.secondary, palette.tertiary}) {
        QVERIFY2(tone.hsvSaturationF() >= 0.24f && tone.hsvSaturationF() <= 0.44f,
                 qPrintable(QStringLiteral("saturation %1 out of [0.24, 0.44] for %2")
                                .arg(tone.hsvSaturationF())
                                .arg(tone.name())));
    }
    QVERIFY2(
        palette.primary.valueF() >= 0.34f && palette.primary.valueF() <= 0.46f,
        qPrintable(
            QStringLiteral("primary value %1 out of [0.34, 0.46]").arg(palette.primary.valueF())));
    QVERIFY2(palette.secondary.valueF() >= 0.28f && palette.secondary.valueF() <= 0.40f,
             qPrintable(QStringLiteral("secondary value %1 out of [0.28, 0.40]")
                            .arg(palette.secondary.valueF())));
    QVERIFY2(palette.tertiary.valueF() >= 0.22f && palette.tertiary.valueF() <= 0.34f,
             qPrintable(QStringLiteral("tertiary value %1 out of [0.22, 0.34]")
                            .arg(palette.tertiary.valueF())));
}

QImage makeTwoTonePoster() {
    QImage image(150, 210, QImage::Format_RGBA8888);
    image.fill(QColor(0x1c, 0x7e, 0xc4)); // vivid steel-cyan blue (~205 deg)
    // Top and bottom bright white logo/border strips that should be ignored.
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(0xf8, 0xf8, 0xf8));
            image.setPixelColor(x, image.height() - 1 - y, QColor(0xf8, 0xf8, 0xf8));
        }
    }
    // Distinct warm crimson-orange accent block on the right third (~11 deg).
    for (int y = 24; y < image.height() - 24; ++y) {
        for (int x = 100; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(0xd4, 0x48, 0x28));
        }
    }
    return image;
}

} // namespace

class AmbientPaletteTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() {
        AmbientPalette::configure(nullptr, QString());
    }

    void solidColorsClampToDarkSafeBounds_data() {
        QTest::addColumn<QColor>("source");
        QTest::newRow("red") << QColor(0xff, 0x20, 0x20);
        QTest::newRow("green") << QColor(0x20, 0xe0, 0x30);
        QTest::newRow("blue") << QColor(0x18, 0x70, 0xff);
        QTest::newRow("gold") << QColor(0xf0, 0xc0, 0x20);
        QTest::newRow("purple") << QColor(0x90, 0x30, 0xe0);
    }

    void solidColorsClampToDarkSafeBounds() {
        QFETCH(QColor, source);
        QImage image(64, 90, QImage::Format_RGBA8888);
        image.fill(source);

        const auto palette = AmbientPalette::extractFromImage(image);
        verifyDarkSafeChromaticBounds(palette);

        const float expected_hue = source.hsvHueF() * 360.0f;
        const float actual_hue = palette.primary.hsvHueF() * 360.0f;
        QVERIFY2(circularHueDistance(expected_hue, actual_hue) <= 20.0f,
                 qPrintable(QStringLiteral("expected hue near %1, got %2 (%3)")
                                .arg(expected_hue)
                                .arg(actual_hue)
                                .arg(palette.primary.name())));
    }

    void neutralAndDegenerateImagesProduceCoolSlate() {
        const QList<QImage> cases = {
            QImage(),
            [] {
                QImage img(64, 90, QImage::Format_RGBA8888);
                img.fill(Qt::transparent);
                return img;
            }(),
            [] {
                QImage img(64, 90, QImage::Format_RGBA8888);
                img.fill(QColor(0, 0, 0));
                return img;
            }(),
            [] {
                QImage img(64, 90, QImage::Format_RGBA8888);
                img.fill(QColor(255, 255, 255));
                return img;
            }(),
            [] {
                QImage img(64, 90, QImage::Format_RGBA8888);
                img.fill(QColor(128, 128, 128));
                return img;
            }(),
        };

        for (const QImage& image : cases) {
            const auto palette = AmbientPalette::extractFromImage(image);
            QVERIFY(palette.primary.isValid());
            QVERIFY(palette.secondary.isValid());
            QVERIFY(palette.tertiary.isValid());
            for (const QColor& tone : {palette.primary, palette.secondary, palette.tertiary}) {
                QVERIFY(tone.hsvSaturationF() <= 0.20f);
                const float hue = tone.hsvHueF() * 360.0f;
                QVERIFY2(hue >= 195.0f && hue <= 235.0f,
                         qPrintable(QStringLiteral("cool slate hue out of range: %1").arg(hue)));
            }
            QVERIFY(palette.primary.valueF() >= 0.34f && palette.primary.valueF() <= 0.46f);
            QVERIFY(palette.secondary.valueF() >= 0.28f && palette.secondary.valueF() <= 0.40f);
            QVERIFY(palette.tertiary.valueF() >= 0.22f && palette.tertiary.valueF() <= 0.34f);
        }
    }

    void twoTonePosterWithWhiteLogoExtractsDistinctTones() {
        const QImage poster = makeTwoTonePoster();
        const auto palette = AmbientPalette::extractFromImage(poster);
        verifyDarkSafeChromaticBounds(palette);

        const float primary_hue = palette.primary.hsvHueF() * 360.0f;
        const float secondary_hue = palette.secondary.hsvHueF() * 360.0f;
        QVERIFY2(
            circularHueDistance(primary_hue, secondary_hue) >= 45.0f,
            qPrintable(QStringLiteral("primary %1 (%2 deg) and secondary %3 (%4 deg) too close")
                           .arg(palette.primary.name())
                           .arg(primary_hue)
                           .arg(palette.secondary.name())
                           .arg(secondary_hue)));
        // Dominant area is steel-cyan blue (~205 deg); secondary block is warm crimson (~11 deg).
        QVERIFY(circularHueDistance(primary_hue, 205.0f) <= 25.0f);
        QVERIFY(circularHueDistance(secondary_hue, 11.0f) <= 25.0f);
    }

    void invalidAndEmptyUrlsReturnFallbackImmediately() {
        AmbientPalette palette;
        const QList<QUrl> invalid_urls = {
            QUrl(),
            QUrl(QStringLiteral("not-a-valid-scheme-url")),
            QUrl(QStringLiteral("ftp://example.com/poster.png")),
        };
        for (const QUrl& url : invalid_urls) {
            const QVariantMap result = palette.colorsFor(url);
            QCOMPARE(result.value(QStringLiteral("primary")).toString(), QStringLiteral("#273549"));
            QCOMPARE(result.value(QStringLiteral("secondary")).toString(),
                     QStringLiteral("#1f293a"));
            QCOMPARE(result.value(QStringLiteral("tertiary")).toString(),
                     QStringLiteral("#181f2c"));
            QCOMPARE(result.value(QStringLiteral("ready")).toBool(), false);
        }
        QCOMPARE(palette.revision(), 0);
    }

    void localFileAsyncExtractionCachesAndReloads() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString poster_path = dir.filePath(QStringLiteral("poster.png"));
        const QString cache_path = dir.filePath(QStringLiteral("cache/ambient-colors.json"));

        const QImage poster = makeTwoTonePoster();
        QVERIFY(poster.save(poster_path, "PNG"));
        const auto expected = AmbientPalette::extractFromImage(poster);
        const QUrl poster_url = QUrl::fromLocalFile(poster_path);

        AmbientPalette::configure(nullptr, cache_path);
        {
            AmbientPalette first;
            const QVariantMap initial = first.colorsFor(poster_url);
            QCOMPARE(initial.value(QStringLiteral("ready")).toBool(), false);
            QCOMPARE(initial.value(QStringLiteral("primary")).toString(),
                     QStringLiteral("#273549"));

            QTRY_VERIFY_WITH_TIMEOUT(first.revision() > 0, 5000);
            const QVariantMap extracted = first.colorsFor(poster_url);
            QCOMPARE(extracted.value(QStringLiteral("ready")).toBool(), true);
            QCOMPARE(extracted.value(QStringLiteral("primary")).toString(),
                     expected.primary.name());
            QCOMPARE(extracted.value(QStringLiteral("secondary")).toString(),
                     expected.secondary.name());
            QCOMPARE(extracted.value(QStringLiteral("tertiary")).toString(),
                     expected.tertiary.name());
        } // Destructor flushes pending debounced save.

        QVERIFY(QFileInfo::exists(cache_path));
        QFile cache_file(cache_path);
        QVERIFY(cache_file.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(cache_file.readAll());
        QVERIFY(doc.isObject());

        AmbientPalette second;
        const QVariantMap reloaded = second.colorsFor(poster_url);
        QCOMPARE(reloaded.value(QStringLiteral("ready")).toBool(), true);
        QCOMPARE(reloaded.value(QStringLiteral("primary")).toString(), expected.primary.name());
        QCOMPARE(reloaded.value(QStringLiteral("secondary")).toString(), expected.secondary.name());
        QCOMPARE(reloaded.value(QStringLiteral("tertiary")).toString(), expected.tertiary.name());
    }

    void httpAsyncExtractionAndCorruptCacheResilience() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString cache_path = dir.filePath(QStringLiteral("ambient-colors.json"));

        // Pre-seed corrupt JSON to ensure startup ignores it gracefully.
        {
            QFile corrupt(cache_path);
            QVERIFY(corrupt.open(QIODevice::WriteOnly));
            corrupt.write("{corrupt-json!!!");
        }

        const QImage poster = makeTwoTonePoster();
        QByteArray png_bytes;
        {
            QBuffer buffer(&png_bytes);
            QVERIFY(buffer.open(QIODevice::WriteOnly));
            QVERIFY(poster.save(&buffer, "PNG"));
        }
        const auto expected = AmbientPalette::extractFromImage(poster);

        HttpTestServer server;
        QVERIFY(server.listen());
        server.route("GET", "/poster.png", [png_bytes](const HttpTestServer::Request&) {
            return HttpTestServer::Response{
                .status = 200,
                .body = png_bytes,
                .headers = {{"Content-Type", "image/png"}},
            };
        });

        QNetworkAccessManager network;
        AmbientPalette::configure(&network, cache_path);

        AmbientPalette palette;
        const QUrl url = server.url(QStringLiteral("/poster.png"));
        const QVariantMap initial = palette.colorsFor(url);
        QCOMPARE(initial.value(QStringLiteral("ready")).toBool(), false);

        QTRY_VERIFY_WITH_TIMEOUT(palette.revision() > 0, 5000);
        const QVariantMap extracted = palette.colorsFor(url);
        QCOMPARE(extracted.value(QStringLiteral("ready")).toBool(), true);
        QCOMPARE(extracted.value(QStringLiteral("primary")).toString(), expected.primary.name());
        QCOMPARE(extracted.value(QStringLiteral("secondary")).toString(),
                 expected.secondary.name());
        QCOMPARE(extracted.value(QStringLiteral("tertiary")).toString(), expected.tertiary.name());

        // Debounced timer flushes valid JSON over the corrupt file without destroying `palette`.
        QTRY_VERIFY_WITH_TIMEOUT(
            [&cache_path] {
                QFile f(cache_path);
                if (!f.open(QIODevice::ReadOnly)) {
                    return false;
                }
                return QJsonDocument::fromJson(f.readAll()).isObject();
            }(),
            2000);
    }
};

QTEST_MAIN(AmbientPaletteTest)
#include "ambient_palette_test.moc"
