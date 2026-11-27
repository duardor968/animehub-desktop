import QtQuick
import AnimeHub

// The selection corner of AnimeHub Web's episode cards: completely invisible at rest, a soft
// translucent corner triangle with a faint diagonal edge and check silhouette on hover (no square
// checkbox), and a solid #2f81f7 corner triangle with a crisp white checkmark once selected.
Item {
    id: root
    objectName: "selectionMark"

    property bool checked: false
    // Set by the parent row while hovering or keyboard-focused so the hover corner previews cleanly.
    property bool rowHovered: false
    // Set by the parent row while batch selection mode is active.
    property bool selecting: false
    // The triangle's sides; sits flush in the top-right corner of the thumbnail.
    property real size: 42
    readonly property bool hovered: area.containsMouse
    readonly property bool activeHover: hovered || rowHovered

    // Shift was held: the click extends the selection over a span.
    signal chosen(bool extend)

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onPressAction: chosen(false)
    Accessible.onToggleAction: chosen(false)

    // Visual corner kept inside a child container so `MouseArea` stays hittable while opacity is 0.
    Item {
        id: visual
        anchors.fill: parent
        clip: true
        opacity: root.checked ? 1.0
                 : root.activeHover ? 0.95
                 : root.selecting ? 0.38 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        // A square rotated 45° about the top-right corner, clipped by `visual`, leaves the exact
        // upper-right triangle with a crisp diagonal edge.
        Rectangle {
            width: root.size * Math.SQRT2
            height: width
            x: root.size - width / 2
            y: -height / 2
            rotation: 45
            antialiasing: true
            color: root.checked
                   ? Theme.accent
                   : (root.hovered ? Qt.rgba(15 / 255, 22 / 255, 34 / 255, 0.86)
                      : root.rowHovered ? Qt.rgba(15 / 255, 22 / 255, 34 / 255, 0.76)
                      : Qt.rgba(15 / 255, 22 / 255, 34 / 255, 0.45))
            border.width: root.checked ? 0 : 1
            border.color: root.hovered ? Qt.rgba(1, 1, 1, 0.34)
                          : root.rowHovered ? Qt.rgba(1, 1, 1, 0.26)
                          : Qt.rgba(1, 1, 1, 0.16)
            Behavior on color { ColorAnimation { duration: 120 } }
        }

        // Crisp white check when selected, subtle check silhouette on hover (no square box).
        Glyph {
            readonly property real inset: Math.max(5, Math.round(root.size * 0.14))
            visible: root.checked || root.activeHover
            x: root.size - 12 - inset
            y: inset
            glyph: Theme.icon.check
            size: 12
            color: root.checked
                   ? Theme.accentInk
                   : (root.hovered ? Qt.rgba(1, 1, 1, 0.84) : Qt.rgba(1, 1, 1, 0.60))
        }
    }

    // Only the upper-right triangle answers the pointer; the rest falls through to the picture.
    // Must be a QtObject with typed `contains(point: point): bool` (Qt 6.11 ignores an untyped JS
    // method and binds an Item mask to QQuickItem::contains on a 0x0 item).
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        containmentMask: QtObject {
            function contains(point: point): bool {
                return point.x >= 0 && point.y >= 0
                       && point.x <= root.size && point.y <= root.size
                       && point.x >= point.y
            }
        }
        onClicked: mouse => root.chosen(mouse.modifiers & Qt.ShiftModifier)
    }
}
