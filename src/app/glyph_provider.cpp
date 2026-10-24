#include "glyph_provider.h"

#include <QFont>
#include <QPainter>

namespace animehub::app {

GlyphProvider::GlyphProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage GlyphProvider::requestImage(const QString& id, QSize* size, const QSize& requested) {
    const int side = requested.isValid() && requested.width() > 0 ? requested.width() : 32;
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    bool ok = false;
    const auto code = id.section(u'?', 0, 0).toUInt(&ok, 16);
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
        painter.setPen(Qt::white); // controls tint it with their icon colour
        painter.drawText(image.rect(), Qt::AlignCenter,
                         QString(QChar(static_cast<char16_t>(code))));
    }
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace animehub::app
