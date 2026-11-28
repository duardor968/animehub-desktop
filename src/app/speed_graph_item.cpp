#include "speed_graph_item.h"

#include <QLinearGradient>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGRenderNode>
#include <QSGRendererInterface>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <vector>

namespace animehub::app {
namespace {

constexpr qreal kPitch = 4.0;
constexpr qreal kBarWidth = 3.0;
constexpr qreal kDashLength = 6.0;
constexpr qreal kDashGap = 3.0;
constexpr qreal kLimitHeight = 1.5;
constexpr qreal kJdAlpha = 0.75;
constexpr qreal kNativeTopAlpha = 0.65;
constexpr qreal kNativeBottomAlpha = 1.0;

struct PremultipliedRgba {
    uchar r = 0;
    uchar g = 0;
    uchar b = 0;
    uchar a = 0;
};

struct ColoredQuad {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    QColor top;
    QColor bottom;
};

[[nodiscard]] QColor withScaledAlpha(const QColor& color, qreal alpha_scale = 1.0) {
    QColor result = color.toRgb();
    result.setAlphaF(
        static_cast<float>(std::clamp(color.alphaF() * alpha_scale, qreal{0.0}, qreal{1.0})));
    return result;
}

[[nodiscard]] PremultipliedRgba premultiply(const QColor& color) {
    const auto a = static_cast<uchar>(std::clamp(color.alpha(), 0, 255));
    const qreal factor = static_cast<qreal>(a) / 255.0;
    const auto r = static_cast<uchar>(std::lround(color.red() * factor));
    const auto g = static_cast<uchar>(std::lround(color.green() * factor));
    const auto b = static_cast<uchar>(std::lround(color.blue() * factor));
    return {r, g, b, a};
}

void appendVerticalGradientRect(QSGGeometry::ColoredPoint2D*& out, float x, float y, float w,
                                float h, PremultipliedRgba top, PremultipliedRgba bottom) {
    const float x2 = x + w;
    const float y2 = y + h;
    out++->set(x, y, top.r, top.g, top.b, top.a);
    out++->set(x2, y, top.r, top.g, top.b, top.a);
    out++->set(x, y2, bottom.r, bottom.g, bottom.b, bottom.a);
    out++->set(x2, y, top.r, top.g, top.b, top.a);
    out++->set(x2, y2, bottom.r, bottom.g, bottom.b, bottom.a);
    out++->set(x, y2, bottom.r, bottom.g, bottom.b, bottom.a);
}

void appendRect(QSGGeometry::ColoredPoint2D*& out, float x, float y, float w, float h,
                PremultipliedRgba color) {
    appendVerticalGradientRect(out, x, y, w, h, color, color);
}

// QSGSoftwareRenderer skips custom QSGGeometryNode + QSGVertexColorMaterial, so software/offscreen
// runs render the same quad list via QSGRenderNode + QPainter while GPU runs keep the single-draw
// QSGGeometryNode path.
class SoftwareGraphNode final : public QSGRenderNode {
public:
    explicit SoftwareGraphNode(QQuickWindow* window) : window_(window) {}

    void update(QQuickWindow* window, QRectF bounds, std::vector<ColoredQuad> quads) {
        window_ = window;
        bounds_ = bounds;
        quads_ = std::move(quads);
        markDirty(DirtyMaterial);
    }

    RenderingFlags flags() const override {
        return BoundedRectRendering;
    }

    QRectF rect() const override {
        return bounds_;
    }

    void render(const RenderState* state) override {
        if (!window_ || !window_->rendererInterface() || quads_.empty()) {
            return;
        }
        auto* painter = static_cast<QPainter*>(window_->rendererInterface()->getResource(
            window_, QSGRendererInterface::PainterResource));
        if (!painter) {
            return;
        }
        painter->save();
        if (state && state->clipRegion() && !state->clipRegion()->isEmpty()) {
            painter->setClipRegion(*state->clipRegion(), Qt::ReplaceClip);
        }
        if (const QMatrix4x4* m = matrix()) {
            painter->setTransform(m->toTransform(), true);
        }
        painter->setOpacity(painter->opacity() * inheritedOpacity());
        painter->setPen(Qt::NoPen);
        for (const ColoredQuad& quad : quads_) {
            const QRectF r(quad.x, quad.y, quad.w, quad.h);
            if (quad.top == quad.bottom) {
                painter->fillRect(r, quad.top);
            } else {
                QLinearGradient grad(quad.x, quad.y, quad.x, quad.y + quad.h);
                grad.setColorAt(0.0, quad.top);
                grad.setColorAt(1.0, quad.bottom);
                painter->fillRect(r, grad);
            }
        }
        painter->restore();
    }

private:
    QQuickWindow* window_ = nullptr;
    QRectF bounds_;
    std::vector<ColoredQuad> quads_;
};

} // namespace

SpeedGraphItem::SpeedGraphItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setImplicitWidth(240.0);
    setImplicitHeight(88.0);
}

void SpeedGraphItem::setSamples(const QList<qreal>& samples) {
    if (samples_ == samples) {
        return;
    }
    samples_ = samples;
    emit samplesChanged();
    scheduleUpdate();
}

void SpeedGraphItem::setNativeSamples(const QList<qreal>& samples) {
    if (native_samples_ == samples) {
        return;
    }
    native_samples_ = samples;
    emit nativeSamplesChanged();
    scheduleUpdate();
}

void SpeedGraphItem::setLimit(qreal limit) {
    const qreal clamped = std::max<qreal>(0.0, limit);
    if (qFuzzyCompare(limit_ + 1.0, clamped + 1.0)) {
        return;
    }
    limit_ = clamped;
    emit limitChanged();
    scheduleUpdate();
}

void SpeedGraphItem::setAccentColor(const QColor& color) {
    if (accent_color_ == color) {
        return;
    }
    accent_color_ = color;
    emit styleChanged();
    scheduleUpdate();
}

void SpeedGraphItem::setJdColor(const QColor& color) {
    if (jd_color_ == color) {
        return;
    }
    jd_color_ = color;
    emit styleChanged();
    scheduleUpdate();
}

void SpeedGraphItem::setLimitColor(const QColor& color) {
    if (limit_color_ == color) {
        return;
    }
    limit_color_ = color;
    emit styleChanged();
    scheduleUpdate();
}

void SpeedGraphItem::setAxisColor(const QColor& color) {
    if (axis_color_ == color) {
        return;
    }
    axis_color_ = color;
    emit styleChanged();
    scheduleUpdate();
}

qsizetype SpeedGraphItem::visibleBarCount(qreal item_width) const {
    if (item_width <= 0.0 || samples_.isEmpty()) {
        return 0;
    }
    const auto slots_fit =
        std::max<qsizetype>(0, static_cast<qsizetype>(std::floor((item_width + 1.0) / kPitch)));
    return std::min(samples_.size(), slots_fit);
}

qreal SpeedGraphItem::nativeSampleAt(qsizetype visible_index, qsizetype bars, qreal total) const {
    if (native_samples_.isEmpty()) {
        return total;
    }
    const qsizetype native_index = native_samples_.size() - bars + visible_index;
    if (native_index < 0 || native_index >= native_samples_.size()) {
        return total;
    }
    return std::clamp(native_samples_.at(native_index), qreal{0.0}, total);
}

void SpeedGraphItem::scheduleUpdate() {
    if (isVisible()) {
        update();
    }
}

void SpeedGraphItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged && value.boolValue) {
        update();
    }
}

void SpeedGraphItem::geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) {
    QQuickItem::geometryChange(new_geometry, old_geometry);
    if (new_geometry.size() != old_geometry.size()) {
        scheduleUpdate();
    }
}

QSGNode* SpeedGraphItem::updatePaintNode(QSGNode* old_node, UpdatePaintNodeData* /*data*/) {
    const bool software_backend =
        window() && window()->rendererInterface() &&
        window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;

    const qreal w = width();
    const qreal h = height();

    std::vector<ColoredQuad> quads;
    if (w > 0.0 && h > 0.0) {
        const qreal plot_height = std::max<qreal>(0.0, h - 1.0);
        const qsizetype bars = visibleBarCount(w);
        const qsizetype start = samples_.size() - bars;

        qreal max_visible = 0.0;
        for (qsizetype i = 0; i < bars; ++i) {
            max_visible = std::max(max_visible, std::max<qreal>(0.0, samples_.at(start + i)));
        }
        const qreal base_max = std::max(limit_, max_visible);
        const qreal highest = base_max > 0.0 ? base_max * 1.05 : 0.0;

        const QColor native_top = withScaledAlpha(accent_color_, kNativeTopAlpha);
        const QColor native_bottom = withScaledAlpha(accent_color_, kNativeBottomAlpha);
        const QColor jd_fill = withScaledAlpha(jd_color_, kJdAlpha);
        const QColor axis_fill = withScaledAlpha(axis_color_);
        const QColor limit_fill = withScaledAlpha(limit_color_);

        const auto dash_count =
            limit_ > 0.0 && highest > 0.0 && h >= 2.0
                ? static_cast<std::size_t>(std::ceil(w / (kDashLength + kDashGap)))
                : std::size_t{0};

        quads.reserve(static_cast<std::size_t>(bars) * 2 + 1 + dash_count);

        // 1. Bars (native gradient lower segment + optional JD upper segment).
        if (highest > 0.0 && plot_height > 0.0) {
            for (qsizetype i = 0; i < bars; ++i) {
                const qreal value = std::max<qreal>(0.0, samples_.at(start + i));
                if (value <= 0.0) {
                    continue;
                }
                const qreal bar_x = w - static_cast<qreal>(bars - i) * kPitch + 1.0;
                const qreal total_h =
                    std::clamp(std::max<qreal>(2.0, std::round((value / highest) * plot_height)),
                               qreal{0.0}, plot_height);
                const qreal native = nativeSampleAt(i, bars, value);

                if (native >= value) {
                    quads.push_back({
                        .x = static_cast<float>(bar_x),
                        .y = static_cast<float>(plot_height - total_h),
                        .w = static_cast<float>(kBarWidth),
                        .h = static_cast<float>(total_h),
                        .top = native_top,
                        .bottom = native_bottom,
                    });
                } else {
                    const qreal native_h =
                        std::clamp(std::round((native / value) * total_h), qreal{0.0}, total_h);
                    const qreal jd_h = total_h - native_h;
                    if (jd_h > 0.0) {
                        quads.push_back({
                            .x = static_cast<float>(bar_x),
                            .y = static_cast<float>(plot_height - total_h),
                            .w = static_cast<float>(kBarWidth),
                            .h = static_cast<float>(jd_h),
                            .top = jd_fill,
                            .bottom = jd_fill,
                        });
                    }
                    if (native_h > 0.0) {
                        quads.push_back({
                            .x = static_cast<float>(bar_x),
                            .y = static_cast<float>(plot_height - native_h),
                            .w = static_cast<float>(kBarWidth),
                            .h = static_cast<float>(native_h),
                            .top = native_top,
                            .bottom = native_bottom,
                        });
                    }
                }
            }
        }

        // 2. Bottom axis line (1px at y = height - 1).
        quads.push_back({
            .x = 0.0f,
            .y = static_cast<float>(h - 1.0),
            .w = static_cast<float>(w),
            .h = 1.0f,
            .top = axis_fill,
            .bottom = axis_fill,
        });

        // 3. Optional horizontal dashed speed-limit line.
        if (limit_ > 0.0 && highest > 0.0 && h >= 2.0) {
            const qreal raw_y = plot_height - std::round((limit_ / highest) * plot_height);
            const qreal limit_y = std::clamp(raw_y, qreal{0.0}, std::max<qreal>(0.0, h - 2.0));
            for (qreal x = 0.0; x < w; x += (kDashLength + kDashGap)) {
                const qreal dash_w = std::min(kDashLength, w - x);
                if (dash_w > 0.0) {
                    quads.push_back({
                        .x = static_cast<float>(x),
                        .y = static_cast<float>(limit_y),
                        .w = static_cast<float>(dash_w),
                        .h = static_cast<float>(kLimitHeight),
                        .top = limit_fill,
                        .bottom = limit_fill,
                    });
                }
            }
        }
    }

    if (software_backend) {
        auto* node = dynamic_cast<SoftwareGraphNode*>(old_node);
        if (!node) {
            delete old_node;
            node = new SoftwareGraphNode(window());
        }
        node->update(window(), QRectF(0.0, 0.0, std::max<qreal>(0.0, w), std::max<qreal>(0.0, h)),
                     std::move(quads));
        return node;
    }

    auto* node = dynamic_cast<QSGGeometryNode*>(old_node);
    QSGGeometry* geometry = nullptr;
    if (!node) {
        delete old_node;
        node = new QSGGeometryNode;
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);

        auto* material = new QSGVertexColorMaterial;
        material->setFlag(QSGMaterial::Blending, true);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    } else {
        geometry = node->geometry();
    }

    geometry->allocate(static_cast<int>(quads.size() * 6));
    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    for (const ColoredQuad& quad : quads) {
        if (quad.top == quad.bottom) {
            appendRect(vertices, quad.x, quad.y, quad.w, quad.h, premultiply(quad.top));
        } else {
            appendVerticalGradientRect(vertices, quad.x, quad.y, quad.w, quad.h,
                                       premultiply(quad.top), premultiply(quad.bottom));
        }
    }

    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

} // namespace animehub::app
