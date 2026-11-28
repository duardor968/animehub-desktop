import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A section of the Downloads page as in Steam: its name, how many it holds, and a hairline to
// the edge, with the section's own actions at the end. A click anywhere on the line folds the
// section; its chevron sits on the right, as in WinUI's Expander, in the column of the rows' own.
Item {
    id: root

    property string text
    property int count
    property bool folded
    default property alias actions: actionRow.data

    signal clicked()

    implicitHeight: Math.max(contentRow.implicitHeight + 12, 36)

    // Fold trigger kept as a sibling of section action buttons (zero nested buttons).
    AbstractButton {
        id: toggleButton
        anchors.fill: parent
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Heading
        Accessible.name: root.text + " (" + root.count + ")"
        // QML has no expanded state: a toggle reads as one.
        Accessible.checkable: true
        Accessible.checked: !root.folded
        Accessible.onPressAction: toggleButton.click()
        Keys.onReturnPressed: event => {
            event.accepted = true
            toggleButton.click()
        }
        Keys.onEnterPressed: event => {
            event.accepted = true
            toggleButton.click()
        }
        onClicked: root.clicked()

        // As the rows': past the content by the same margin on both sides.
        background: Rectangle {
            x: -16
            width: toggleButton.width + 32
            radius: Theme.radius
            color: toggleButton.down ? Theme.raised
                   : toggleButton.hovered ? Theme.hover : "transparent"

            // The keyboard's frame, as every control's.
            FocusFrame {
                visible: toggleButton.activeFocus && InputModality.keyboard
                radiusInside: Theme.radius
            }
        }

        contentItem: Item {}
    }

    // The catalog's headings follow the same measures: the count 8 px after the name, the
    // line 12 px after the count.
    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 0

        Label {
            text: root.text
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }

        Label {
            Layout.leftMargin: 8
            text: "(" + root.count + ")"
            color: Theme.muted
            font.pixelSize: Theme.subtitle
            Accessible.ignored: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: 1
            color: Theme.stroke
        }

        Row {
            id: actionRow
            Layout.leftMargin: 12
            visible: children.length > 0
            spacing: 4
        }

        Glyph {
            Layout.leftMargin: 12
            Layout.preferredWidth: 16
            glyph: root.folded ? Theme.icon.chevronDown : Theme.icon.chevronUp
            size: 12
            color: Theme.muted
        }
    }
}
