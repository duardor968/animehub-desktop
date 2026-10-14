#ifndef ANIMEHUB_APP_GLYPH_PROVIDER_H
#define ANIMEHUB_APP_GLYPH_PROVIDER_H

#include <QQuickImageProvider>

namespace animehub::app {

// Serves Segoe Fluent Icons glyphs as images (`image://glyph/e768`), so controls can take them
// as `icon.source` and tint them like any other icon.
class GlyphProvider final : public QQuickImageProvider {
public:
    GlyphProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_GLYPH_PROVIDER_H
