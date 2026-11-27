import QtQuick
import AnimeHub

// Atmospheric hero/header backdrop: shows the real artwork when an anime has a backdrop, or a
// silky multi-point dominant-color ambient glow extracted from the poster in C++ when it does
// not — never a blurred stretched poster with ghost text.
Item {
    id: root

    property url backdropUrl
    property url posterUrl
    // Surface colour the bottom edge fades seamlessly into (Theme.pane on headers, Theme.canvas on Hero).
    property color surfaceColor: Theme.pane
    // Optional darker scrim on the left side under hero typography.
    property bool leftScrim: true
    // Multiplies the ambient glow intensity (e.g. 0.85 on compact surfaces, 1.0 on headers).
    property real glowStrength: 1.0

    readonly property bool hasBackdrop: backdropUrl.toString() !== "" && backdrop.status !== Image.Error
    readonly property var ambient: {
        const _ = AmbientPalette.revision
        return AmbientPalette.colorsFor(posterUrl)
    }

    clip: true

    // Base dark canvas so there is never an unstyled flash while images decode.
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
    }

    // Real backdrop when available.
    Image {
        id: backdrop
        anchors.fill: parent
        visible: root.hasBackdrop
        source: root.backdropUrl
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignTop
        asynchronous: true
        sourceSize.width: Math.min(1920, Math.max(640, Math.ceil(width * Screen.devicePixelRatio)))
        opacity: status === Image.Ready ? 0.82 : 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }

    // Dominant-color radial glow when there is no backdrop; painted only when colors or size change.
    Canvas {
        id: glow
        anchors.fill: parent
        visible: !root.hasBackdrop
        renderStrategy: Canvas.Cooperative

        readonly property string c1: root.ambient.primary ?? "#273549"
        readonly property string c2: root.ambient.secondary ?? "#1f293a"
        readonly property string c3: root.ambient.tertiary ?? "#181f2c"
        readonly property color baseColor: root.surfaceColor
        readonly property real strength: root.glowStrength

        onC1Changed: requestPaint()
        onC2Changed: requestPaint()
        onC3Changed: requestPaint()
        onBaseColorChanged: requestPaint()
        onStrengthChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function rgba(hex, alpha) {
            const c = Qt.color(hex)
            const a = Math.max(0, Math.min(1, alpha))
            return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
                   + Math.round(c.b * 255) + "," + a.toFixed(3) + ")"
        }

        onPaint: {
            const ctx = getContext("2d")
            const w = width
            const h = height
            if (w <= 0 || h <= 0)
                return

            ctx.reset()

            // Base vertical tint slightly lifted at top.
            const baseGrad = ctx.createLinearGradient(0, 0, 0, h)
            baseGrad.addColorStop(0.0, "#161b24")
            baseGrad.addColorStop(0.85, baseColor.toString())
            baseGrad.addColorStop(1.0, baseColor.toString())
            ctx.fillStyle = baseGrad
            ctx.fillRect(0, 0, w, h)

            const s = strength

            // 1. Primary elliptical glow anchored behind poster & title zone (left-center).
            ctx.save()
            ctx.translate(w * 0.18, h * 0.36)
            ctx.scale(1.0, Math.max(0.5, (h * 1.25) / Math.max(1, w * 0.62)))
            const r1 = Math.max(120, w * 0.62)
            const g1 = ctx.createRadialGradient(0, 0, 0, 0, 0, r1)
            g1.addColorStop(0.0, rgba(c1, 0.62 * s))
            g1.addColorStop(0.5, rgba(c1, 0.20 * s))
            g1.addColorStop(0.82, rgba(c1, 0.0))
            ctx.fillStyle = g1
            ctx.fillRect(-r1, -r1, r1 * 2, r1 * 2)
            ctx.restore()

            // 2. Secondary elliptical glow upper-right for depth and diagonal balance.
            ctx.save()
            ctx.translate(w * 0.76, h * 0.18)
            ctx.scale(1.0, Math.max(0.5, (h * 1.15) / Math.max(1, w * 0.56)))
            const r2 = Math.max(120, w * 0.56)
            const g2 = ctx.createRadialGradient(0, 0, 0, 0, 0, r2)
            g2.addColorStop(0.0, rgba(c2, 0.50 * s))
            g2.addColorStop(0.52, rgba(c2, 0.14 * s))
            g2.addColorStop(0.80, rgba(c2, 0.0))
            ctx.fillStyle = g2
            ctx.fillRect(-r2, -r2, r2 * 2, r2 * 2)
            ctx.restore()

            // 3. Subtle tertiary top-center fill bridging the two lights.
            ctx.save()
            ctx.translate(w * 0.48, h * 0.08)
            ctx.scale(1.0, Math.max(0.45, (h * 0.9) / Math.max(1, w * 0.44)))
            const r3 = Math.max(100, w * 0.44)
            const g3 = ctx.createRadialGradient(0, 0, 0, 0, 0, r3)
            g3.addColorStop(0.0, rgba(c3, 0.28 * s))
            g3.addColorStop(0.72, rgba(c3, 0.0))
            ctx.fillStyle = g3
            ctx.fillRect(-r3, -r3, r3 * 2, r3 * 2)
            ctx.restore()
        }
    }

    // Bottom fade into the surrounding surface so neither backdrop nor glow ends on a hard seam.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, root.hasBackdrop ? 0.12 : 0.04) }
            GradientStop { position: 0.55; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, root.hasBackdrop ? 0.52 : 0.28) }
            GradientStop { position: 0.86; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.92) }
            GradientStop { position: 1.0; color: root.surfaceColor }
        }
    }

    // Optional horizontal scrim so titles and controls on the left stay high-contrast over bright art.
    Rectangle {
        anchors.fill: parent
        visible: root.leftScrim && root.hasBackdrop
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.84) }
            GradientStop { position: 0.42; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.56) }
            GradientStop { position: 0.78; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.14) }
            GradientStop { position: 1.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.0) }
        }
    }
}
