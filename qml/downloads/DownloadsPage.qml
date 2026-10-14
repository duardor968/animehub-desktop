import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The download queue: what is transferring, what waits, and what has finished.
Item {
    id: page

    required property AppController app
    readonly property DownloadsView downloads: app.downloads

    signal browseCatalog()
    signal openAnime(string slug)

    RowLayout {
        id: toolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        anchors.leftMargin: 32
        spacing: 8
        visible: page.downloads.count > 0

        Button {
            visible: page.downloads.activeCount > 0
            text: page.downloads.anyPaused ? "Reanudar todo" : "Pausar todo"
            icon.source: Theme.source(page.downloads.anyPaused ? Theme.icon.play : Theme.icon.pause)
            onClicked: page.downloads.anyPaused ? page.downloads.resumeAll() : page.downloads.pauseAll()
        }

        Button {
            text: "Abrir la carpeta"
            icon.source: Theme.source(Theme.icon.folder)
            onClicked: page.app.openDownloadFolder()
        }

        Item { Layout.fillWidth: true }

        Button {
            visible: page.downloads.finishedCount > 0
            flat: true
            text: "Quitar las terminadas"
            onClicked: page.downloads.clearFinished()
        }
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: 8
        clip: true
        model: page.downloads
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: "Descargas"

        section.property: "section"
        section.delegate: Label {
            required property string section
            leftPadding: 32
            topPadding: 16
            bottomPadding: 6
            text: section
            color: Theme.muted
            font.pixelSize: Theme.caption
            font.weight: Font.DemiBold
        }

        delegate: ItemDelegate {
            id: row
            required property int index
            required property var taskId
            required property string slug
            required property string title
            required property string episodeLabel
            required property url posterUrl
            required property string state
            required property real progress
            required property string detail
            required property bool problem

            readonly property bool active: state === "downloading" || state === "resolving"
                                           || state === "verifying"
            readonly property bool finished: state === "completed" || state === "failed"
                                             || state === "cancelled"

            width: ListView.view.width
            height: 72
            leftPadding: 32
            rightPadding: 24
            Accessible.name: title + ", " + episodeLabel + ", " + detail
            onClicked: ListView.view.currentIndex = index
            onDoubleClicked: if (state === "completed") page.downloads.play(taskId)

            contentItem: RowLayout {
                spacing: 14

                Poster {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 54
                    source: row.posterUrl
                    radius: 3
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: row.title + " · " + row.episodeLabel
                        color: Theme.ink
                        font.pixelSize: Theme.body
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    ProgressLine {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 520
                        visible: !row.finished
                        value: row.progress
                    }

                    Label {
                        Layout.fillWidth: true
                        text: row.detail
                        color: row.problem ? Theme.danger : Theme.muted
                        font.pixelSize: Theme.caption
                        elide: Text.ElideRight
                    }
                }

                ToolButton {
                    visible: row.state === "completed"
                    icon.source: Theme.source(Theme.icon.play)
                    display: AbstractButton.IconOnly
                    text: "Ver"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.play(row.taskId)
                }
                ToolButton {
                    visible: row.state === "completed"
                    icon.source: Theme.source(Theme.icon.folder)
                    display: AbstractButton.IconOnly
                    text: "Mostrar en la carpeta"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.showFile(row.taskId)
                }
                ToolButton {
                    visible: row.active || row.state === "queued" || row.state === "waiting"
                    enabled: row.state !== "verifying"
                    icon.source: Theme.source(Theme.icon.pause)
                    display: AbstractButton.IconOnly
                    text: "Pausar"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.pause(row.taskId)
                }
                ToolButton {
                    visible: row.state === "paused"
                    icon.source: Theme.source(Theme.icon.play)
                    display: AbstractButton.IconOnly
                    text: "Reanudar"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.resume(row.taskId)
                }
                ToolButton {
                    visible: row.state === "failed" || row.state === "waiting"
                    icon.source: Theme.source(Theme.icon.retry)
                    display: AbstractButton.IconOnly
                    text: row.state === "waiting" ? "Reintentar ahora" : "Reintentar"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.retry(row.taskId)
                }
                ToolButton {
                    visible: !row.finished
                    icon.source: Theme.source(Theme.icon.cancel)
                    display: AbstractButton.IconOnly
                    text: "Cancelar"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.cancel(row.taskId)
                }
                ToolButton {
                    visible: row.finished
                    icon.source: Theme.source(Theme.icon.cancel)
                    display: AbstractButton.IconOnly
                    text: "Quitar de la lista"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.downloads.forget(row.taskId)
                }
                ToolButton {
                    icon.source: Theme.source(Theme.icon.library)
                    display: AbstractButton.IconOnly
                    text: "Ir al anime"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: page.openAnime(row.slug)
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: page.downloads.count === 0
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "No hay descargas"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Explorar el catálogo"
            highlighted: true
            onClicked: page.browseCatalog()
        }
    }
}
