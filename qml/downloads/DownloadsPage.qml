import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Downloads, by series as in Steam: the download on view and the speed at the top, then the
// series in line and the completed ones.
Item {
    id: page

    required property AppController app
    readonly property DownloadsView downloads: app.downloads

    signal browseCatalog()
    signal openAnime(string slug)
    // The gear: the download settings.
    signal settingsRequested()

    // Unfolded series, by slug; kept while a series moves between sections.
    property var expanded: ({})
    function toggle(slug) {
        const next = Object.assign({}, expanded)
        if (next[slug])
            delete next[slug]
        else
            next[slug] = true
        expanded = next
    }

    // Width of the progress column of every row.
    readonly property int barWidth: width >= 1400 ? 280 : width >= 1100 ? 220 : width >= 900 ? 160 : 136

    // Dragging a series in line: the place it would take; the rows between make room for it.
    property int dragFrom: -1
    property int dropTo: -1
    property real dragHeight: 0
    function placeFor(row) {
        const center = row.y + row.dragOffset + row.height / 2
        let slot = 0
        for (let i = 0; i < queueList.count; ++i) {
            const item = queueList.itemAtIndex(i)
            if (item && center > item.y + item.height / 2)
                slot = i + 1
        }
        return slot > row.index ? slot - 1 : slot
    }
    function trackDrag(row) {
        dragFrom = row.index
        dragHeight = row.height
        dropTo = placeFor(row)
    }
    function dropSeries(from) {
        const to = dropTo
        dragFrom = -1
        dropTo = -1
        if (to >= 0 && to !== from)
            downloads.moveSeries(from, to)
    }
    // How far a row steps aside while another is dragged over it.
    function displacement(index) {
        if (dragFrom < 0 || index === dragFrom)
            return 0
        if (index > dragFrom && index <= dropTo)
            return -dragHeight
        if (index < dragFrom && index >= dropTo)
            return dragHeight
        return 0
    }

    function askCancel(slug, title, consequence) {
        if (consequence === "") {
            downloads.cancelSeries(slug)
            return
        }
        cancelDialog.slug = slug
        cancelDialog.taskId = -1
        cancelDialog.title = "¿Cancelar la descarga de " + title + "?"
        cancelDialog.consequence = consequence
        cancelDialog.open()
    }
    function askCancelEpisode(taskId, label, title, partial) {
        if (partial === "") {
            downloads.cancel(taskId)
            return
        }
        cancelDialog.taskId = taskId
        cancelDialog.title = "¿Cancelar la descarga del " + label.toLowerCase() + " de " + title + "?"
        cancelDialog.consequence = "Se borrarán los " + partial + " descargados."
        cancelDialog.open()
    }
    function askRemove(slug, title, files, diskText) {
        if (files === 0) {
            downloads.forgetSeries(slug)
            return
        }
        removeDialog.slug = slug
        removeDialog.title = "¿Quitar " + title + " de Descargas?"
        removeDialog.consequence = diskText + " en disco."
        removeDialog.open()
    }

    DownloadsHeader {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        downloads: page.downloads
    }

    RowLayout {
        id: toolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        anchors.topMargin: 16
        spacing: 8

        Button {
            visible: page.downloads.activeCount > 0 || page.downloads.anyPaused
            text: page.downloads.activeCount > 0 ? "Pausar todo" : "Reanudar todo"
            icon.source: Theme.source(page.downloads.activeCount > 0 ? Theme.icon.pause : Theme.icon.download)
            onClicked: page.downloads.activeCount > 0 ? page.downloads.pauseAll() : page.downloads.resumeAll()
        }

        Button {
            text: "Abrir la carpeta"
            icon.source: Theme.source(Theme.icon.folder)
            onClicked: page.app.openDownloadFolder()
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            icon.source: Theme.source(Theme.icon.settings)
            display: AbstractButton.IconOnly
            text: "Ajustes de descargas"
            ToolTip.visible: hovered
            ToolTip.text: text
            onClicked: page.settingsRequested()
        }
    }

    Flickable {
        id: scroll
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: 8
        clip: true
        contentWidth: width
        contentHeight: sections.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drag moves a series in line, not the page; the wheel scrolls it.
        acceptedButtons: Qt.NoButton
        ScrollBar.vertical: ScrollBar {}

        // The edge the list passes under once scrolled.
        Rectangle {
            parent: scroll
            z: 1
            width: scroll.width
            height: 1
            color: Theme.stroke
            opacity: scroll.contentY > 0 ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }

        ColumnLayout {
            id: sections
            width: scroll.width
            spacing: 0

            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                text: "A continuación"
                count: page.downloads.queue.count
            }

            ColumnLayout {
                visible: page.downloads.queue.count === 0
                Layout.leftMargin: 32
                Layout.topMargin: 8
                spacing: 16

                Label {
                    text: "No hay descargas en cola"
                    color: Theme.muted
                    font.pixelSize: Theme.body
                }

                Button {
                    text: "Explorar el catálogo"
                    highlighted: true
                    onClicked: page.browseCatalog()
                }
            }

            ListView {
                id: queueList
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                interactive: false
                model: page.downloads.queue
                Accessible.name: "A continuación"

                move: Transition { NumberAnimation { property: "y"; duration: 200; easing.type: Easing.OutCubic } }
                moveDisplaced: Transition { NumberAnimation { property: "y"; duration: 200; easing.type: Easing.OutCubic } }
                displaced: Transition { NumberAnimation { property: "y"; duration: 200; easing.type: Easing.OutCubic } }
                add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 } }
                remove: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 150 } }

                delegate: SeriesRow {
                    id: row
                    downloads: page.downloads
                    queued: true
                    expanded: page.expanded[slug] === true
                    barWidth: page.barWidth
                    displacement: page.displacement(index)
                    onToggled: page.toggle(slug)
                    onMenuRequested: anchor => queueMenu.showFor(row, anchor)
                    onEpisodeCancelRequested: (taskId, label, partial) =>
                        page.askCancelEpisode(taskId, label, title, partial)
                    onMoveRequested: step => page.downloads.moveSeries(index, index + step)
                    onDragOffsetChanged: if (dragging) page.trackDrag(row)
                    onDragFinished: page.dropSeries(index)
                }
            }

            SectionHeading {
                visible: page.downloads.completed.count > 0
                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                text: "Completadas"
                count: page.downloads.completed.count

                Button {
                    flat: true
                    text: "Quitar de la lista"
                    onClicked: page.downloads.clearCompleted()
                }
            }

            ListView {
                id: completedList
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                Layout.bottomMargin: 24
                interactive: false
                model: page.downloads.completed
                Accessible.name: "Completadas"

                displaced: Transition { NumberAnimation { property: "y"; duration: 200; easing.type: Easing.OutCubic } }
                add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 } }
                remove: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 150 } }

                delegate: SeriesRow {
                    id: done
                    downloads: page.downloads
                    queued: false
                    expanded: page.expanded[slug] === true
                    barWidth: page.barWidth
                    onToggled: page.toggle(slug)
                    onMenuRequested: anchor => completedMenu.showFor(done, anchor)
                }
            }
        }
    }

    // One menu for every series in line.
    ActionMenu {
        id: queueMenu

        property string slug
        property string name
        property bool first
        property bool hasFolder
        property string consequence

        entries: [nowEntry, folderEntry, animeEntry, cancelSeparator, cancelEntry]

        function showFor(row, anchor) {
            slug = row.slug
            name = row.title
            first = row.first
            hasFolder = row.hasFolder
            consequence = row.cancelText
            queueMenu.show(anchor, true)
        }

        MenuEntry {
            id: nowEntry
            property bool wanted: !queueMenu.first
            text: "Descargar ahora"
            glyph: Theme.icon.download
            onTriggered: page.downloads.prioritize(queueMenu.slug)
        }
        MenuEntry {
            id: folderEntry
            text: "Abrir la carpeta"
            enabled: queueMenu.hasFolder
            glyph: Theme.icon.folder
            onTriggered: page.downloads.openSeriesFolder(queueMenu.slug)
        }
        MenuEntry {
            id: animeEntry
            text: "Ir al anime"
            glyph: Theme.icon.library
            onTriggered: page.openAnime(queueMenu.slug)
        }
        MenuSeparator {
            id: cancelSeparator
            property bool separator: true
        }
        MenuEntry {
            id: cancelEntry
            text: queueMenu.consequence !== "" ? "Cancelar la descarga…" : "Cancelar la descarga"
            glyph: Theme.icon.cancel
            onTriggered: page.askCancel(queueMenu.slug, queueMenu.name, queueMenu.consequence)
        }
    }

    // One menu for every completed series.
    ActionMenu {
        id: completedMenu

        property string slug
        property string name
        property int files
        property string diskText

        function showFor(row, anchor) {
            slug = row.slug
            name = row.title
            files = row.filesOnDisk
            diskText = row.diskText
            completedMenu.show(anchor, true)
        }

        MenuEntry {
            text: "Ir al anime"
            glyph: Theme.icon.library
            onTriggered: page.openAnime(completedMenu.slug)
        }
        MenuEntry {
            text: completedMenu.files > 0 ? "Quitar…" : "Quitar de la lista"
            glyph: Theme.icon.remove
            onTriggered: page.askRemove(completedMenu.slug, completedMenu.name, completedMenu.files,
                                        completedMenu.diskText)
        }
    }

    ModalDialog {
        id: cancelDialog

        property string slug
        property var taskId: -1
        property string consequence

        // Wide enough for the title, which names the series.
        width: Math.min(Math.max(460, implicitHeaderWidth), parent.width - 48)

        Label {
            width: cancelDialog.availableWidth
            text: cancelDialog.consequence
            color: Theme.ink
            wrapMode: Text.Wrap
        }

        footer: DialogButtonBox {
            Button {
                text: "Cancelar la descarga"
                // DialogButtonBox clears a `highlighted` set at creation; a binding comes back.
                highlighted: cancelDialog.visible
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Seguir descargando"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: taskId >= 0 ? page.downloads.cancel(taskId) : page.downloads.cancelSeries(slug)
    }

    ModalDialog {
        id: removeDialog

        property string slug
        property string consequence

        width: Math.min(Math.max(520, implicitHeaderWidth), parent.width - 48)

        Label {
            width: removeDialog.availableWidth
            text: removeDialog.consequence
            color: Theme.ink
            wrapMode: Text.Wrap
        }

        footer: DialogButtonBox {
            Button {
                text: "Quitar de la lista"
                highlighted: removeDialog.visible
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Mover a la Papelera"
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    removeDialog.close()
                    page.downloads.trashSeries(removeDialog.slug)
                }
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: page.downloads.forgetSeries(slug)
    }
}
