import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Downloads, by series as in Steam: the series on view and the speed at the top, then the
// other series in line and the completed ones, each section folded as it was left. With nothing
// in line, what the library has to download is one click away.
Item {
    id: page

    required property AppController app
    readonly property DownloadsView downloads: app.downloads
    readonly property LibraryModel library: app.library
    readonly property bool empty: downloads.current.count === 0 && downloads.queue.count === 0
                                  && downloads.completed.count === 0

    signal browseCatalog()
    signal openAnime(string slug)

    // INI settings come back as text.
    property bool queueFolded: String(app.settings.value("downloads/queueFolded", false)) === "true"
    property bool completedFolded: String(app.settings.value("downloads/completedFolded", false)) === "true"
    onQueueFoldedChanged: app.settings.setValue("downloads/queueFolded", queueFolded)
    onCompletedFoldedChanged: app.settings.setValue("downloads/completedFolded", completedFolded)

    // A section's body: it folds into its heading and opens out of it. Only the opening is
    // animated, so its rows unfold and move at their own pace; folded, nothing in it takes focus.
    component Fold: Item {
        property bool open
        property real shown: open ? 1 : 0
        default property alias content: body.data

        Layout.fillWidth: true
        implicitHeight: Math.round(body.implicitHeight * shown)
        visible: shown > 0
        opacity: shown
        clip: true
        Behavior on shown { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        ColumnLayout {
            id: body
            width: parent.width
            spacing: 0
        }
    }

    // As the library's "Descargar N episodios": large batches are confirmed first.
    function downloadPending() {
        const episodes = library.pendingCount
        const series = library.downloadCount
        const start = () => {
            const failure = library.downloadPending()
            if (failure)
                app.message(failure, true)
            else
                app.message(episodes === 1 ? "1 episodio en cola" : episodes + " episodios en cola", false)
        }
        if (episodes < 25) {
            start()
            return
        }
        confirm.ask("¿Descargar " + episodes + " episodios?",
                    "De " + series + (series === 1 ? " anime" : " animes") + ", en "
                    + app.settings.downloadDirectory + ".",
                    "Descargar", "Cancelar", start)
    }

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
        expanded: page.expanded[page.downloads.headerSlug] === true
        onToggled: slug => page.toggle(slug)
        onMenuRequested: (row, anchor) => queueMenu.showFor(row, anchor, true)
        onFolderRequested: page.app.openDownloadFolder()
    }

    Flickable {
        id: scroll
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
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

            // The episodes of the series on view, unfolded from the header.
            Repeater {
                model: page.downloads.current

                Item {
                    id: unfolded

                    required property string slug
                    required property string title
                    required property var episodes
                    readonly property bool open: page.expanded[slug] === true

                    Layout.fillWidth: true
                    implicitHeight: open ? currentColumn.implicitHeight : 0
                    clip: true
                    opacity: open ? 1 : 0
                    Behavior on implicitHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    Column {
                        id: currentColumn
                        width: parent.width
                        topPadding: 8
                        bottomPadding: 8

                        Repeater {
                            model: unfolded.open || unfolded.height > 0 ? unfolded.episodes : null

                            EpisodeLine {
                                width: currentColumn.width
                                downloads: page.downloads
                                indent: 32
                                barWidth: page.barWidth
                                actionsWidth: 84
                                trailing: 24
                                onCancelRequested: (taskId, label, partial) =>
                                    page.askCancelEpisode(taskId, label, unfolded.title, partial)
                            }
                        }
                    }
                }
            }

            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                text: "A continuación"
                count: page.downloads.queue.count
                folded: page.queueFolded
                onClicked: page.queueFolded = !page.queueFolded
            }

            Fold {
                open: !page.queueFolded

                // Nothing after the series on view, but episodes the library is missing: where
                // someone who only downloads starts, with the library's own button.
                Button {
                    visible: page.downloads.queue.count === 0 && page.library.pendingCount > 0
                    Layout.leftMargin: 32
                    Layout.bottomMargin: 8
                    text: page.library.pendingCount === 1 ? "Descargar 1 episodio"
                                                          : "Descargar " + page.library.pendingCount + " episodios"
                    highlighted: true
                    icon.source: Theme.source(Theme.icon.download)
                    onClicked: page.downloadPending()
                }

                RowLayout {
                    visible: page.empty && page.library.pendingCount === 0
                    Layout.leftMargin: 32
                    Layout.bottomMargin: 8
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
            }

            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                text: "Completadas"
                count: page.downloads.completed.count
                folded: page.completedFolded
                onClicked: page.completedFolded = !page.completedFolded

                // The files stay; only the list empties.
                Button {
                    visible: page.downloads.completed.count > 0
                    flat: true
                    text: "Quitar todas"
                    onClicked: page.downloads.clearCompleted()
                }
            }

            Fold {
                open: !page.completedFolded
                Layout.bottomMargin: 24

                ListView {
                    id: completedList
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
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
    }

    // One menu for every series in line; the header's also holds the page's own entry.
    ActionMenu {
        id: queueMenu

        property string slug
        property string name
        property bool first
        property bool hasFolder
        property string consequence
        property bool withPage

        entries: [nowEntry, folderEntry, animeEntry, cancelSeparator, cancelEntry, pageSeparator,
            downloadsFolderEntry]

        function showFor(row, anchor, fromHeader) {
            slug = row.slug
            name = row.title
            first = row.first
            hasFolder = row.hasFolder
            consequence = row.cancelText
            withPage = fromHeader === true
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
        MenuSeparator {
            id: pageSeparator
            property bool separator: true
        }
        MenuEntry {
            id: downloadsFolderEntry
            property bool wanted: queueMenu.withPage
            text: "Abrir la carpeta de descargas"
            glyph: Theme.icon.folder
            onTriggered: page.app.openDownloadFolder()
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

    ConfirmDialog { id: confirm }

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
