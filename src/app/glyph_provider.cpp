#include "glyph_provider.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace animehub::app {
namespace {

// Builds a closed polygon with uniform subtle quadratic-bezier rounded corners at every vertex.
QPainterPath roundedPolygon(const QList<QPointF>& pts, qreal radius) {
    QPainterPath path;
    const qsizetype n = pts.size();
    if (n < 3) {
        return path;
    }

    auto unit = [](const QPointF& v) -> QPointF {
        const qreal len = std::hypot(v.x(), v.y());
        return len > 1e-6 ? QPointF(v.x() / len, v.y() / len) : QPointF(0, 0);
    };

    for (qsizetype i = 0; i < n; ++i) {
        const QPointF prev = pts[(i + n - 1) % n];
        const QPointF curr = pts[i];
        const QPointF next = pts[(i + 1) % n];

        const QPointF vIn = prev - curr;
        const QPointF vOut = next - curr;
        const qreal lenIn = std::hypot(vIn.x(), vIn.y());
        const qreal lenOut = std::hypot(vOut.x(), vOut.y());
        const qreal r = std::min({radius, lenIn * 0.35, lenOut * 0.35});

        const QPointF pStart = curr + unit(vIn) * r;
        const QPointF pEnd = curr + unit(vOut) * r;

        if (i == 0) {
            path.moveTo(pStart);
        } else {
            path.lineTo(pStart);
        }
        path.quadTo(curr, pEnd);
    }
    path.closeSubpath();
    return path;
}

QColor parseColorQuery(const QString& id) {
    const QString queryStr = id.section(u'?', 1);
    if (queryStr.isEmpty()) {
        return Qt::white;
    }
    const QUrlQuery query(queryStr);
    const QString raw = query.queryItemValue(QStringLiteral("color"));
    if (raw.isEmpty()) {
        return Qt::white;
    }
    const QString hex = raw.startsWith(u'#') ? raw : (QLatin1Char('#') + raw);
    const QColor c = QColor::fromString(hex);
    return c.isValid() ? c : Qt::white;
}

bool isCustomIcon(const QString& name) {
    return name == u"pot-play" || name == u"pot-pause" || name == u"pot-stop" ||
           name == u"bookmark-ribbon";
}

QImage renderCustomIcon(const QString& name, int side, const QColor& color) {
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal s = static_cast<qreal>(side);
    const qreal cx = s * 0.5;
    const qreal cy = s * 0.5;

    if (name == u"pot-play") {
        // PotPlayer-style solid filled geometric right-pointing triangle,
        // optically shifted +6% right of center with subtle corner rounding.
        const qreal h = s * 0.72;
        const qreal w = h * 0.88;
        const qreal shiftX = s * 0.06;
        const qreal leftX = cx + shiftX - w * 0.5;
        const qreal rightX = cx + shiftX + w * 0.5;
        const qreal topY = cy - h * 0.5;
        const qreal botY = cy + h * 0.5;

        const QList<QPointF> tri = {
            QPointF(leftX, topY),
            QPointF(rightX, cy),
            QPointF(leftX, botY),
        };
        painter.drawPath(roundedPolygon(tri, h * 0.085));
    } else if (name == u"pot-pause") {
        // PotPlayer-style solid twin vertical bars with subtle rounding, centered.
        const qreal barW = s * 0.26;
        const qreal barH = s * 0.68;
        const qreal gap = s * 0.18;
        const qreal totalW = barW * 2.0 + gap;
        const qreal leftX = cx - totalW * 0.5;
        const qreal rightX = leftX + barW + gap;
        const qreal topY = cy - barH * 0.5;
        const qreal r = std::clamp(s * 0.075, 0.8, 1.8);

        painter.drawRoundedRect(QRectF(leftX, topY, barW, barH), r, r);
        painter.drawRoundedRect(QRectF(rightX, topY, barW, barH), r, r);
    } else if (name == u"pot-stop") {
        // Solid filled rounded square.
        const qreal box = s * 0.60;
        const qreal r = std::clamp(s * 0.08, 0.9, 2.0);
        painter.drawRoundedRect(QRectF(cx - box * 0.5, cy - box * 0.5, box, box), r, r);
    } else if (name == u"bookmark-ribbon") {
        // Solid vertical bookmark ribbon silhouette with clean V-notch tail at bottom.
        const qreal w = s * 0.64;
        const qreal h = s * 0.88;
        const qreal x0 = cx - w * 0.5;
        const qreal y0 = cy - h * 0.5;
        const qreal x1 = x0 + w;
        const qreal y1 = y0 + h;
        const qreal notchY = y1 - w * 0.44;
        const qreal topR = std::clamp(s * 0.06, 0.8, 2.0);

        QPainterPath ribbon;
        ribbon.moveTo(x0, y0 + topR);
        ribbon.quadTo(x0, y0, x0 + topR, y0);
        ribbon.lineTo(x1 - topR, y0);
        ribbon.quadTo(x1, y0, x1, y0 + topR);
        ribbon.lineTo(x1, y1);
        ribbon.lineTo(cx, notchY);
        ribbon.lineTo(x0, y1);
        ribbon.closeSubpath();
        painter.drawPath(ribbon);
    }

    return image;
}

} // namespace

GlyphProvider::GlyphProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage GlyphProvider::requestImage(const QString& id, QSize* size, const QSize& requested) {
    const int side = requested.width() > 0    ? requested.width()
                     : requested.height() > 0 ? requested.height()
                                              : 32;
    const auto key =
        id + QLatin1Char('@') + QString::number(side) + QLatin1Char('x') + QString::number(side);
    {
        QMutexLocker lock(&mutex_);
        if (const auto* cached = cache_.object(key)) {
            if (size != nullptr) {
                *size = cached->size();
            }
            return *cached;
        }
    }

    const QString glyph = id.section(u'?', 0, 0);
    const QColor color = parseColorQuery(id);
    QImage image;
    if (isCustomIcon(glyph)) {
        image = renderCustomIcon(glyph, side, color);
    } else {
        image = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        bool ok = false;
        const auto code = glyph.toUInt(&ok, 16);
        if (ok && code > 0 && code <= 0x10FFFF) {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::TextAntialiasing);
#ifdef Q_OS_WIN
            QFont font(QStringLiteral("Segoe Fluent Icons"));
#else
            QFont font(QStringLiteral("Symbols")); // Uno Fluent Icons, loaded by main()
#endif
            font.setPixelSize(side);
            painter.setFont(font);
            painter.setPen(color); // default white so controls tint it with their icon colour
            painter.drawText(image.rect(), Qt::AlignCenter,
                             QString(QChar(static_cast<char16_t>(code))));
        }
    }
    {
        QMutexLocker lock(&mutex_);
        cache_.insert(key, new QImage(image));
    }
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace animehub::app
