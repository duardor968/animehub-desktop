import QtQuick
import AnimeHub

// Thin QML wrapper over the native C++ Scene Graph SpeedGraphItem: renders stacked native and
// JDownloader bars, the bottom hairline, and the dashed coral speed-limit threshold in a single
// draw call with zero per-bar QQuickItem children and zero work while hidden.
SpeedGraphItem {
    id: root

    implicitWidth: 240
    implicitHeight: 72
    accentColor: Theme.accent
    jdColor: Theme.muted
    limitColor: Theme.limitLine
    axisColor: Theme.stroke
    Accessible.role: Accessible.Graphic
    Accessible.name: "Gráfica de velocidad de descarga"
}
