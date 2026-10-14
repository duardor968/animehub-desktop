import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Once the header scrolls away: the way back, the title and the main action stay at hand.
Rectangle {
    id: root

    required property AnimeView view
    property string backText

    signal back()

    implicitHeight: 56
    color: Theme.pane

    // The bar covers the list: presses on its empty parts must not reach the rows beneath.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = false
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 32
        spacing: 12

        ToolButton {
            visible: root.backText !== ""
            icon.source: Theme.source(Theme.icon.back)
            display: AbstractButton.IconOnly
            text: "Volver a " + root.backText
            ToolTip.visible: hovered
            ToolTip.text: text
            onClicked: root.back()
        }

        Label {
            Layout.fillWidth: true
            text: root.view.title
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        PrimaryActions {
            view: root.view
            compact: true
        }
    }
}
