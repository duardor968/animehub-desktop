import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A section of the Downloads page as in Steam: its name, how many it holds, and a hairline to
// the edge, with the section's own actions at the end. A click anywhere on the line folds the
// section; its chevron sits on the right, as in WinUI's Expander, in the column of the rows' own.
AbstractButton {
    id: root

    property int count
    property bool folded
    default property alias actions: actionRow.data

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    topPadding: 6
    bottomPadding: 6
    Accessible.role: Accessible.Heading
    Accessible.name: text + " (" + count + ")"
    Accessible.description: folded ? "Plegada" : "Desplegada"
    Accessible.onPressAction: clicked()

    // As the rows': past the content by the same margin on both sides.
    background: Rectangle {
        x: -16
        width: root.width + 32
        radius: Theme.radius
        color: root.hovered ? Theme.hover : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.accent
    }

    contentItem: RowLayout {
        spacing: 12

        Label {
            text: root.text
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }

        Label {
            text: "(" + root.count + ")"
            color: Theme.faint
            font.pixelSize: Theme.subtitle
            Accessible.ignored: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: 1
            color: Theme.stroke
        }

        Row {
            id: actionRow
            visible: children.length > 0
            spacing: 4
        }

        Glyph {
            Layout.preferredWidth: 16
            glyph: root.folded ? "e70d" : "e70e"
            size: 12
            color: Theme.muted
        }
    }
}
