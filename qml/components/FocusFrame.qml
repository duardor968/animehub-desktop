import QtQuick
import AnimeHub

// The keyboard's focus frame as Fluent draws it around its own controls (a light ring outside a
// dark hairline, 3 px out), for the pieces that are not Fluent controls: one focus style
// everywhere. It frames its parent, or a sibling as `target` (a clipping parent would cut it).
Rectangle {
    property Item target: parent
    property real radiusInside: Theme.radius

    anchors.fill: target
    anchors.margins: -3
    z: 100
    radius: radiusInside + 3
    color: "transparent"
    border.color: "white"
    border.width: 2

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: parent.radiusInside + 1
        color: "transparent"
        border.color: "black"
        border.width: 1
    }
}
