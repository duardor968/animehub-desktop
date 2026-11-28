#ifndef ANIMEHUB_APP_SPEED_GRAPH_ITEM_H
#define ANIMEHUB_APP_SPEED_GRAPH_ITEM_H

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QtQmlIntegration>

namespace animehub::app {

// Native scene-graph speed history graph (bars + bottom axis + optional dashed limit line).
// Renders in a single QSGGeometryNode draw call and skips updates while hidden.
class SpeedGraphItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<qreal> samples READ samples WRITE setSamples NOTIFY samplesChanged)
    Q_PROPERTY(QList<qreal> nativeSamples READ nativeSamples WRITE setNativeSamples NOTIFY
                   nativeSamplesChanged)
    Q_PROPERTY(qreal limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY styleChanged)
    Q_PROPERTY(QColor jdColor READ jdColor WRITE setJdColor NOTIFY styleChanged)
    Q_PROPERTY(QColor limitColor READ limitColor WRITE setLimitColor NOTIFY styleChanged)
    Q_PROPERTY(QColor axisColor READ axisColor WRITE setAxisColor NOTIFY styleChanged)

public:
    explicit SpeedGraphItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QList<qreal> samples() const {
        return samples_;
    }
    void setSamples(const QList<qreal>& samples);

    [[nodiscard]] QList<qreal> nativeSamples() const {
        return native_samples_;
    }
    void setNativeSamples(const QList<qreal>& samples);

    [[nodiscard]] qreal limit() const {
        return limit_;
    }
    void setLimit(qreal limit);

    [[nodiscard]] QColor accentColor() const {
        return accent_color_;
    }
    void setAccentColor(const QColor& color);

    [[nodiscard]] QColor jdColor() const {
        return jd_color_;
    }
    void setJdColor(const QColor& color);

    [[nodiscard]] QColor limitColor() const {
        return limit_color_;
    }
    void setLimitColor(const QColor& color);

    [[nodiscard]] QColor axisColor() const {
        return axis_color_;
    }
    void setAxisColor(const QColor& color);

signals:
    void samplesChanged();
    void nativeSamplesChanged();
    void limitChanged();
    void styleChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData* data) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) override;

private:
    [[nodiscard]] qsizetype visibleBarCount(qreal item_width) const;
    [[nodiscard]] qreal nativeSampleAt(qsizetype visible_index, qsizetype bars, qreal total) const;
    void scheduleUpdate();

    QList<qreal> samples_;
    QList<qreal> native_samples_;
    qreal limit_ = 0.0;
    QColor accent_color_{0x2f, 0x81, 0xf7};
    QColor jd_color_{0x9a, 0xa1, 0xab};
    QColor limit_color_{0xff, 0x9d, 0x8f};
    QColor axis_color_{0x2a, 0x2f, 0x37};
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_SPEED_GRAPH_ITEM_H
