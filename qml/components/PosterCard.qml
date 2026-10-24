import QtQuick
import QtQuick.Controls
import AnimeHub

// An anime as a poster with its title on two lines and one line under it; a title cut even so
// shows whole in a tooltip. Hovered, the poster offers what the card is for: play the episode
// or download it.
AbstractButton {
    id: root

    property url posterUrl
    property string caption
    property real progress: -1
    property bool fresh: false
    // In the library: a check on the poster's corner, as Steam marks what one owns.
    property bool owned: false
    // "play", "download" or nothing: the button over the poster.
    property string quickAction

    // The poster's own button was pressed, not the card.
    signal act()

    implicitWidth: 156
    implicitHeight: poster.height + titleLabel.height + captionLabel.height + 14
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: (caption ? text + ", " + caption : text) + (owned ? ", en la biblioteca" : "")

    background: null
    ToolTip.visible: hovered && titleLabel.truncated
    ToolTip.delay: 500
    ToolTip.text: text

    contentItem: Item {
        Poster {
            id: poster
            width: parent.width
            height: Math.round(width * 1.42)
            source: root.posterUrl
            scale: root.hovered ? 1.03 : 1
            Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

            // The new episode stands on the poster, as Steam's badges do.
            Label {
                visible: root.fresh
                x: 6
                y: 6
                leftPadding: 6
                rightPadding: 6
                topPadding: 2
                bottomPadding: 2
                text: "Nuevo"
                color: Theme.accentInk
                font.pixelSize: Theme.caption
                font.weight: Font.DemiBold
                background: Rectangle {
                    radius: 3
                    color: Theme.accent
                }
            }

            // SelectionMark's plate, opaque and with its light edge, so it reads over dark
            // pictures too; the check in green, not the accent, which marks a choice.
            Rectangle {
                visible: root.owned
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                width: 22
                height: 22
                radius: 5
                color: Theme.canvas
                border.width: 1.5
                border.color: Qt.rgba(1, 1, 1, 0.75)
                ToolTip.visible: ownedHover.hovered
                ToolTip.text: "En la biblioteca"

                HoverHandler { id: ownedHover }

                Glyph {
                    anchors.centerIn: parent
                    glyph: Theme.icon.check
                    size: 14
                    color: Theme.success
                }
            }

            RoundButton {
                visible: root.quickAction !== "" && (root.hovered || root.visualFocus)
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 8
                highlighted: true
                text: root.quickAction === "play" ? "Ver" : "Descargar"
                display: AbstractButton.IconOnly
                icon.source: Theme.source(root.quickAction === "play" ? Theme.icon.play : Theme.icon.download)
                focusPolicy: Qt.NoFocus
                onClicked: root.act()
                Accessible.name: text + ", " + root.text
                ToolTip.visible: hovered
                ToolTip.text: text
            }

            ProgressLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: root.progress >= 0
                value: root.progress
                radius: 0
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.accent
                border.width: 2
                visible: root.visualFocus
            }
        }

        Label {
            id: titleLabel
            anchors.top: poster.bottom
            anchors.topMargin: 8
            width: parent.width
            text: root.text
            // Always two lines high, so the captions of a row line up.
            height: Math.ceil(2 * titleMetrics.lineSpacing) + 4
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop

            FontMetrics {
                id: titleMetrics
                font: titleLabel.font
            }
        }

        Label {
            id: captionLabel
            anchors.top: titleLabel.bottom
            anchors.topMargin: 2
            width: parent.width
            text: root.caption
            color: Theme.muted
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
        }
    }
}
