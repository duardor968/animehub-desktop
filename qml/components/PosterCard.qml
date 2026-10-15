import QtQuick
import QtQuick.Controls
import AnimeHub

// An anime as a poster with its title and one line under it.
AbstractButton {
    id: root

    property url posterUrl
    property string caption
    property real progress: -1
    property bool fresh: false

    implicitWidth: 156
    implicitHeight: poster.height + titleLabel.height + captionLabel.height + 14
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: caption ? text + ", " + caption : text

    background: null
    contentItem: Item {
        FontMetrics {
            id: titleMetrics
            font: titleLabel.font
        }

        Poster {
            id: poster
            width: parent.width
            height: Math.round(width * 1.42)
            source: root.posterUrl
            scale: root.hovered ? 1.02 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }

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
                radius: Theme.radius
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
            // Always two lines tall: captions of a row line up whatever the title's length.
            height: 2 * titleMetrics.lineSpacing
            verticalAlignment: Text.AlignTop
            text: root.text
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
        }

        // New episodes read as in the library list: a dot and the accent colour.
        Row {
            id: captionRow
            anchors.top: titleLabel.bottom
            anchors.topMargin: 2
            width: parent.width
            spacing: 6

            Rectangle {
                visible: root.fresh
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 6
                radius: 3
                color: Theme.accent
            }

            Label {
                id: captionLabel
                width: parent.width - (root.fresh ? 12 : 0)
                text: root.caption
                color: root.fresh ? Theme.accent : Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
            }
        }
    }
}
