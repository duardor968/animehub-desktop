#ifndef ANIMEHUB_APP_GLYPH_PROVIDER_H
#define ANIMEHUB_APP_GLYPH_PROVIDER_H

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace animehub::app {

// Serves Segoe Fluent Icons glyphs (`image://glyph/e768`) and solid geometric vector icons
// (`pot-play`, `pot-pause`, `pot-stop`, `bookmark-ribbon`), with optional `?color=RRGGBB`
// tinting (default white so controls can still tint with `icon.color`).
class GlyphProvider final : public QQuickImageProvider {
public:
    GlyphProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;

private:
    QMutex mutex_;
    QCache<QString, QImage> cache_{256};
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_GLYPH_PROVIDER_H
