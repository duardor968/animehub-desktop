import QtQuick
import QtQuick.Controls
import AnimeHub

// The check mark on a thumbnail's corner, as in Windows' Photos and on the web: an opaque plate
// so it reads over any picture, filled with the accent once chosen. Not a restyled CheckBox:
// Fluent's is transparent and fades into dark frames. It only shows the choice; whoever places
// it makes it, from `chosen`.
Item {
    id: root

    property bool checked: false
    readonly property bool hovered: area.containsMouse

    // Shift was held: the click extends the selection over a span.
    signal chosen(bool extend)

    implicitWidth: 22
    implicitHeight: 22
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onPressAction: chosen(false)
    Accessible.onToggleAction: chosen(false)
    ToolTip.visible: hovered
    ToolTip.delay: 300
    ToolTip.text: checked ? "Quitar de la selección" : "Seleccionar"

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.checked ? Theme.accent : Qt.rgba(0, 0, 0, 0.6)
        border.width: 1.5
        border.color: root.checked ? Theme.accent
                    : root.hovered ? Theme.ink : Qt.rgba(1, 1, 1, 0.75)
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Glyph {
        anchors.centerIn: parent
        glyph: Theme.icon.check
        size: 12
        color: Theme.accentInk
        visible: root.checked
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => root.chosen(mouse.modifiers & Qt.ShiftModifier)
    }
}
