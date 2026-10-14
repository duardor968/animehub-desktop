import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The download in progress, at the bottom of every page but Downloads.
AbstractButton {
    id: root

    required property DownloadsView downloads

    implicitHeight: 36
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: "Descargas: " + downloads.currentTitle + " " + downloads.currentDetail

    background: Rectangle {
        color: root.hovered ? Theme.hover : Theme.pane

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.stroke
        }
    }

    contentItem: RowLayout {
        spacing: 10

        Item { Layout.preferredWidth: 6 }

        Glyph {
            glyph: Theme.icon.download
            size: 14
            color: Theme.accent
        }

        Label {
            text: root.downloads.currentTitle || "Descargas en espera"
            color: Theme.ink
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
            Layout.maximumWidth: 420
        }

        ProgressLine {
            visible: root.downloads.currentTitle !== ""
            Layout.preferredWidth: 160
            value: root.downloads.currentProgress
        }

        Label {
            text: root.downloads.currentDetail
            color: Theme.muted
            font.pixelSize: Theme.caption
        }

        Item { Layout.fillWidth: true }

        Label {
            text: root.downloads.queueText
            color: Theme.muted
            font.pixelSize: Theme.caption
        }

        Item { Layout.preferredWidth: 10 }
    }
}
