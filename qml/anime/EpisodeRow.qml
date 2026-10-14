import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One episode: what it is, how far it was watched, and what can be done with it.
ItemDelegate {
    id: row

    required property AnimeView view
    required property EpisodeMenu menu
    required property int index
    required property real number
    required property string label
    required property string title
    required property url imageUrl
    required property int watchState
    required property real progress
    required property string progressText
    required property bool fresh
    required property bool hasFile
    required property string downloadState
    required property real downloadProgress
    required property string downloadText

    readonly property bool watched: watchState === 2
    readonly property bool transferring: downloadState !== "" && downloadState !== "failed"

    height: 76
    leftPadding: 32
    rightPadding: 24
    Accessible.name: label + (title ? ", " + title : "") + (watched ? ", visto" : "")
    onDoubleClicked: if (hasFile) view.play(number)
    onClicked: ListView.view.currentIndex = index
    Keys.onReturnPressed: if (hasFile) view.play(number)

    contentItem: RowLayout {
        spacing: 16

        Item {
            Layout.preferredWidth: 104
            Layout.preferredHeight: 58

            Poster {
                anchors.fill: parent
                source: row.imageUrl
                opacity: row.watched ? 0.5 : 1
            }

            ProgressLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: row.progress >= 0
                value: row.progress
                radius: 0
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: row.label
                    color: row.watched ? Theme.muted : Theme.ink
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                }

                Label {
                    visible: row.fresh
                    text: "Nuevo"
                    color: Theme.accent
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: row.title
                color: Theme.muted
                elide: Text.ElideRight
                font.pixelSize: Theme.caption
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: row.watched || row.progressText !== ""

                Glyph {
                    visible: row.watched
                    glyph: Theme.icon.check
                    size: 12
                    color: Theme.success
                }

                Label {
                    Layout.fillWidth: true
                    text: row.watched ? "Visto" : row.progressText
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                }
            }
        }

        ColumnLayout {
            visible: row.downloadState !== ""
            // A nested layout inherits fillWidth from its children; this one keeps its size.
            Layout.fillWidth: false
            Layout.preferredWidth: 200
            spacing: 4

            ProgressLine {
                Layout.fillWidth: true
                visible: row.transferring
                value: row.downloadProgress
            }

            Label {
                Layout.fillWidth: true
                text: row.downloadText
                color: row.downloadState === "failed" ? Theme.danger : Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
            }
        }

        ToolButton {
            visible: row.hasFile
            icon.source: Theme.source(Theme.icon.play)
            display: AbstractButton.IconOnly
            text: "Ver " + row.label.toLowerCase()
            ToolTip.visible: hovered
            ToolTip.text: "Ver"
            onClicked: row.view.play(row.number)
        }

        ToolButton {
            visible: !row.hasFile && row.downloadState === ""
            icon.source: Theme.source(Theme.icon.download)
            display: AbstractButton.IconOnly
            text: "Descargar " + row.label.toLowerCase()
            ToolTip.visible: hovered
            ToolTip.text: "Descargar"
            onClicked: row.view.download(row.number)
        }

        ToolButton {
            visible: row.downloadState === "downloading" || row.downloadState === "queued"
                     || row.downloadState === "waiting" || row.downloadState === "resolving"
            icon.source: Theme.source(Theme.icon.pause)
            display: AbstractButton.IconOnly
            text: "Pausar la descarga"
            ToolTip.visible: hovered
            ToolTip.text: "Pausar"
            onClicked: row.view.pauseDownload(row.number)
        }

        ToolButton {
            visible: row.downloadState === "paused"
            icon.source: Theme.source(Theme.icon.play)
            display: AbstractButton.IconOnly
            text: "Reanudar la descarga"
            ToolTip.visible: hovered
            ToolTip.text: "Reanudar"
            onClicked: row.view.resumeDownload(row.number)
        }

        ToolButton {
            visible: row.downloadState === "failed"
            icon.source: Theme.source(Theme.icon.retry)
            display: AbstractButton.IconOnly
            text: "Reintentar la descarga"
            ToolTip.visible: hovered
            ToolTip.text: "Reintentar"
            onClicked: row.view.retryDownload(row.number)
        }

        ToolButton {
            id: more
            icon.source: Theme.source(Theme.icon.more)
            display: AbstractButton.IconOnly
            text: "Opciones de " + row.label.toLowerCase()
            ToolTip.visible: hovered
            ToolTip.text: "Más opciones"
            onClicked: row.menu.show(row, more)
        }
    }
}
