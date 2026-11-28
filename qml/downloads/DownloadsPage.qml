import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Downloads, by series as in Steam: compact free-scrolling Hero at the top (scrolling cleanly out
// of view inside the Flickable), configurable episode expansion ("cards" / "compact") for the Hero
// and every series row, then "También descargando", "Pendientes en tu biblioteca",
// "A continuación" and "Completadas".
Item {
    id: page

    required property AppController app
    readonly property DownloadsView downloads: app.downloads
    readonly property LibraryModel library: app.library
    readonly property bool empty: downloads.current.count === 0 && downloads.active.count === 0
                                  && downloads.queue.count === 0 && downloads.completed.count === 0
    readonly property bool compactEpisodes: app.settings.downloadEpisodesStyle === "compact"
    readonly property bool narrow: width < 920

    signal browseCatalog()
    signal openAnime(string slug)

    // INI settings come back as text.
    property bool queueFolded: String(app.settings.value("downloads/queueFolded", false)) === "true"
    property bool completedFolded: String(app.settings.value("downloads/completedFolded", false)) === "true"
    property bool activeFolded: String(app.settings.value("downloads/activeFolded", false)) === "true"
    property bool pendingFolded: false
    onActiveFoldedChanged: app.settings.setValue("downloads/activeFolded", activeFolded)
    onQueueFoldedChanged: app.settings.setValue("downloads/queueFolded", queueFolded)
    onCompletedFoldedChanged: app.settings.setValue("downloads/completedFolded", completedFolded)

    // A section's body: it folds into its heading and opens out of it. Folded, nothing takes focus.
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
                    + app.settings.nativeDownloadDirectory + ".",
                    "Descargar", "Cancelar", start)
    }

    // Keep whatever the keyboard reaches inside the scroll pane cleanly in view.
    function ensureVisible(item) {
        if (!item || !page.visible || !InputModality.keyboard)
            return
        let inside = false
        for (let p = item; p && !inside; p = p.parent)
            inside = p === sections
        if (!inside)
            return
        const top = item.mapToItem(sections, 0, 0).y
        if (top - 16 < scroll.contentY)
            scroll.contentY = Math.max(0, top - 16)
        else if (top + item.height + 16 > scroll.contentY + scroll.height)
            scroll.contentY = Math.min(top + item.height + 16 - scroll.height,
                                       Math.max(0, scroll.contentHeight - scroll.height))
    }

    Connections {
        target: page.Window.window
        function onActiveFocusItemChanged() {
            page.ensureVisible(page.Window.activeFocusItem)
        }
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

    // Width of the progress column of queued/completed rows.
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
    function displacement(index) {
        if (dragFrom < 0 || index === dragFrom)
            return 0
        if (index > dragFrom && index <= dropTo)
            return -dragHeight
        if (index < dragFrom && index >= dropTo)
            return dragHeight
        return 0
    }

    // "Cancelar 7 episodios", "Cancelar el episodio 3": consistent scope phrasing.
    function cancelScope(count) {
        return count === 1 ? "Cancelar 1 episodio" : "Cancelar " + count + " episodios"
    }
    function askCancel(slug, title, consequence, count) {
        if (consequence === "") {
            downloads.cancelSeries(slug)
            return
        }
        cancelDialog.slug = slug
        cancelDialog.taskId = -1
        cancelDialog.title = "¿" + cancelScope(count) + " de " + title + "?"
        cancelDialog.action = cancelScope(count)
        cancelDialog.consequence = consequence
        cancelDialog.open()
    }
    function askCancelEpisode(taskId, label, title, partial) {
        if (partial === "") {
            downloads.cancel(taskId)
            return
        }
        cancelDialog.taskId = taskId
        cancelDialog.title = "¿Cancelar el " + label.toLowerCase() + " de " + title + "?"
        cancelDialog.action = "Cancelar el " + label.toLowerCase()
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

    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: sections.implicitHeight + 16
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drag moves a series in line, not the page; the wheel scrolls it.
        acceptedButtons: Qt.NoButton
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: sections
            width: scroll.width
            spacing: 0

            // Compact free-scrolling Steam Hero (scrolls upward with the page).
            DownloadsHeader {
                id: header
                Layout.fillWidth: true
                downloads: page.downloads
                settings: page.app.settings
                expanded: page.expanded[page.downloads.headerSlug] === true
                onToggled: slug => page.toggle(slug)
                onAnimeRequested: slug => page.openAnime(slug)
                onMenuRequested: (row, anchor) => queueMenu.showFor(row, anchor, true)
                onFolderRequested: page.app.openDownloadFolder()
            }

            // Expanded episodes of the Hero anime, directly from downloads.headerTasks.
            Item {
                id: heroEpisodesBox

                readonly property bool open: page.downloads.headerMode !== DownloadsView.Idle
                                             && page.downloads.headerTaskCount > 0
                                             && page.expanded[page.downloads.headerSlug] === true
                property real shown: open ? 1 : 0

                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(heroExpansionWrap.implicitHeight * shown)
                implicitHeight: Layout.preferredHeight
                visible: shown > 0
                opacity: shown
                clip: true
                Behavior on shown { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                Item {
                    id: heroExpansionWrap
                    width: parent.width
                    implicitHeight: page.compactEpisodes ? (heroCompactPanel.implicitHeight + 16)
                                                         : (heroCardsPanel.implicitHeight + 20)

                    // OPTION B ("cards", default): Elevated Theme.raised panel + 2 px #2f81f7 left guide.
                    Rectangle {
                        id: heroCardsPanel
                        visible: !page.compactEpisodes
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 32
                        anchors.rightMargin: 32
                        anchors.topMargin: 12
                        implicitHeight: heroCardsColumn.implicitHeight + 12
                        height: implicitHeight
                        radius: Theme.radius
                        color: Theme.raised
                        border.width: 1
                        border.color: Theme.stroke
                        clip: true

                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: 1
                            anchors.top: parent.top
                            anchors.topMargin: 6
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            width: 2
                            radius: 1
                            color: Theme.accent
                        }

                        Column {
                            id: heroCardsColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 4
                            anchors.topMargin: 6

                            Repeater {
                                model: !page.compactEpisodes && (heroEpisodesBox.open || heroEpisodesBox.shown > 0)
                                       ? page.downloads.headerTasks : null

                                EpisodeLine {
                                    width: heroCardsColumn.width
                                    downloads: page.downloads
                                    compactMode: false
                                    narrow: page.narrow
                                    showDivider: index < page.downloads.headerTaskCount - 1
                                    onCancelRequested: (taskId, label, partial) =>
                                        page.askCancelEpisode(taskId, label, page.downloads.headerTitle, partial)
                                }
                            }
                        }
                    }

                    // OPTION C ("compact"): Dense 36 px 1-line table with column header.
                    Column {
                        id: heroCompactPanel
                        visible: page.compactEpisodes
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 32
                        anchors.rightMargin: 32
                        anchors.topMargin: 8

                        Item {
                            width: parent.width
                            height: 24

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: page.narrow ? 10 : 12

                                Label {
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: 68
                                    Layout.minimumWidth: 68
                                    Layout.maximumWidth: 68
                                    text: "EP."
                                    color: Theme.faint
                                    font.pixelSize: Theme.micro
                                    font.weight: Font.Bold
                                }
                                Label {
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: page.narrow ? 132 : 210
                                    Layout.minimumWidth: page.narrow ? 132 : 210
                                    Layout.maximumWidth: page.narrow ? 132 : 210
                                    text: "PROGRESO"
                                    color: Theme.faint
                                    font.pixelSize: Theme.micro
                                    font.weight: Font.Bold
                                }
                                Label {
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: page.narrow ? 92 : 120
                                    Layout.minimumWidth: page.narrow ? 92 : 120
                                    Layout.maximumWidth: page.narrow ? 92 : 120
                                    text: "ESTADO"
                                    color: Theme.faint
                                    font.pixelSize: Theme.micro
                                    font.weight: Font.Bold
                                }
                                Label {
                                    visible: !page.narrow
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: 96
                                    Layout.minimumWidth: 96
                                    Layout.maximumWidth: 96
                                    text: "VELOCIDAD"
                                    color: Theme.faint
                                    font.pixelSize: Theme.micro
                                    font.weight: Font.Bold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: "TAMAÑO · RESTANTE"
                                    color: Theme.faint
                                    font.pixelSize: Theme.micro
                                    font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                                Item {
                                    Layout.preferredWidth: 112
                                    Layout.minimumWidth: 112
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Theme.stroke
                            opacity: 0.7
                        }

                        Repeater {
                            model: page.compactEpisodes && (heroEpisodesBox.open || heroEpisodesBox.shown > 0)
                                   ? page.downloads.headerTasks : null

                            EpisodeLine {
                                width: heroCompactPanel.width
                                downloads: page.downloads
                                compactMode: true
                                narrow: page.narrow
                                showDivider: index < page.downloads.headerTaskCount - 1
                                onCancelRequested: (taskId, label, partial) =>
                                    page.askCancelEpisode(taskId, label, page.downloads.headerTitle, partial)
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Theme.stroke
                            opacity: 0.8
                        }
                    }
                }
            }

            // Other series transferring in parallel right below the Hero's expanded episodes.
            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                visible: page.downloads.active.count > 0
                text: "También descargando"
                count: page.downloads.active.count
                folded: page.activeFolded
                onClicked: page.activeFolded = !page.activeFolded
            }

            Fold {
                open: !page.activeFolded && page.downloads.active.count > 0

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    interactive: false
                    model: page.downloads.active
                    Accessible.name: "También descargando"

                    displaced: Transition { NumberAnimation { property: "y"; duration: 200; easing.type: Easing.OutCubic } }
                    add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 } }
                    remove: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 150 } }

                    delegate: SeriesRow {
                        id: activeRow
                        downloads: page.downloads
                        activeSection: true
                        queued: true
                        draggable: false
                        compactEpisodes: page.compactEpisodes
                        expanded: page.expanded[slug] === true
                        barWidth: page.barWidth
                        onToggled: page.toggle(slug)
                        onMenuRequested: anchor => queueMenu.showFor(activeRow, anchor)
                        onEpisodeCancelRequested: (taskId, label, partial) =>
                            page.askCancelEpisode(taskId, label, title, partial)
                    }
                }
            }

            // Nothing after the series on view, but episodes the library is missing: shown under
            // its own heading with the pending series rows instead of an empty void.
            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                visible: page.downloads.queue.count === 0 && page.library.pendingCount > 0
                text: "Pendientes en tu biblioteca"
                count: page.library.downloadCount
                folded: page.pendingFolded
                onClicked: page.pendingFolded = !page.pendingFolded

                AccentButton {
                    visible: page.library.pendingCount > 1
                    text: "Descargar " + page.library.pendingCount + " episodios"
                    glyphCode: Theme.icon.download
                    onClicked: page.downloadPending()
                }
            }

            Fold {
                open: !page.pendingFolded && page.downloads.queue.count === 0
                      && page.library.pendingCount > 0

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    interactive: false
                    model: page.library.downloadShelf
                    Accessible.name: "Pendientes en tu biblioteca"

                    delegate: Item {
                        id: pendingItem

                        required property string slug
                        required property string title
                        required property url posterUrl
                        required property string caption

                        width: ListView.view ? ListView.view.width : 0
                        height: 84

                        HoverHandler { id: pendingHover }

                        // Row open trigger kept as a sibling of AccentButton (zero nested buttons).
                        AbstractButton {
                            id: pendingRow
                            anchors.fill: parent
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.role: Accessible.Button
                            Accessible.name: pendingItem.title + ", " + pendingItem.caption
                            Accessible.onPressAction: pendingRow.click()
                            Keys.onReturnPressed: event => {
                                event.accepted = true
                                pendingRow.click()
                            }
                            Keys.onEnterPressed: event => {
                                event.accepted = true
                                pendingRow.click()
                            }
                            onClicked: page.openAnime(pendingItem.slug)

                            background: Rectangle {
                                x: 16
                                width: parent.width - 32
                                radius: Theme.radius
                                color: pendingRow.down ? Theme.raised
                                       : (pendingRow.hovered || pendingHover.hovered)
                                         ? Theme.hover : "transparent"
                                FocusFrame {
                                    visible: pendingRow.activeFocus && InputModality.keyboard
                                    radiusInside: Theme.radius
                                }
                            }

                            contentItem: Item {}
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 32
                            anchors.rightMargin: 32
                            spacing: 16

                            Poster {
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 68
                                Layout.alignment: Qt.AlignVCenter
                                source: pendingItem.posterUrl
                                Accessible.ignored: true
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 3

                                Label {
                                    Layout.fillWidth: true
                                    text: pendingItem.title
                                    color: Theme.ink
                                    font.pixelSize: Theme.body
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: pendingItem.caption
                                    color: Theme.muted
                                    font.pixelSize: Theme.caption
                                    elide: Text.ElideRight
                                }
                            }

                            AccentButton {
                                Layout.alignment: Qt.AlignVCenter
                                text: "Descargar"
                                emphasized: page.empty && page.library.pendingCount === 1
                                glyphCode: Theme.icon.download
                                Accessible.name: "Descargar " + pendingItem.title
                                onClicked: {
                                    const failure = page.library.downloadPendingOf(pendingItem.slug)
                                    if (failure)
                                        page.app.message(failure, true)
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 32
                            anchors.rightMargin: 32
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.stroke
                            opacity: 0.5
                        }
                    }
                }
            }

            // Only with something in it, or the empty-state prompt when nothing is pending anywhere.
            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                visible: page.downloads.queue.count > 0
                         || (page.empty && page.library.pendingCount === 0)
                text: "A continuación"
                count: page.downloads.queue.count
                folded: page.queueFolded
                onClicked: page.queueFolded = !page.queueFolded
            }

            Fold {
                open: !page.queueFolded
                      && (page.downloads.queue.count > 0
                          || (page.empty && page.library.pendingCount === 0))

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

                    AccentButton {
                        text: "Explorar el catálogo"
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
                        compactEpisodes: page.compactEpisodes
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

            // Only with something in it.
            SectionHeading {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.bottomMargin: 8
                visible: page.downloads.completed.count > 0
                text: "Completadas"
                count: page.downloads.completed.count
                folded: page.completedFolded
                onClicked: page.completedFolded = !page.completedFolded

                // The files stay; confirm before clearing the completed list.
                Button {
                    id: clearCompletedButton
                    visible: page.downloads.completed.count > 0
                    text: "Quitar todas"
                    Accessible.name: "Quitar todas las descargas completadas de la lista"
                    Accessible.onPressAction: clearCompletedButton.click()
                    Keys.onReturnPressed: event => {
                        event.accepted = true
                        clearCompletedButton.click()
                    }
                    Keys.onEnterPressed: event => {
                        event.accepted = true
                        clearCompletedButton.click()
                    }
                    onClicked: clearCompletedDialog.open()
                }
            }

            Fold {
                open: !page.completedFolded && page.downloads.completed.count > 0
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
                        compactEpisodes: page.compactEpisodes
                        expanded: page.expanded[slug] === true
                        barWidth: page.barWidth
                        onToggled: page.toggle(slug)
                        onMenuRequested: anchor => completedMenu.showFor(done, anchor)
                        onEpisodeCancelRequested: (taskId, label, partial) =>
                            page.askCancelEpisode(taskId, label, title, partial)
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
        // Waiting its turn, it can be held: the row's own button is for what transfers.
        property bool holdable
        property bool canRetryNow
        property bool hasFolder
        property string consequence
        property int cancelCount
        property bool withPage

        entries: [retryNowEntry, nowEntry, holdEntry, folderEntry, animeEntry, cancelSeparator, cancelEntry, pageSeparator,
            downloadsFolderEntry]

        function showFor(row, anchor, fromHeader) {
            slug = row.slug
            name = row.title
            first = row.first
            holdable = row.canPause && !row.running
            canRetryNow = row.canRetryNow
            hasFolder = row.hasFolder
            consequence = row.cancelText
            cancelCount = row.cancelCount
            withPage = fromHeader === true
            queueMenu.show(anchor, true)
        }

        MenuEntry {
            id: retryNowEntry
            property bool wanted: queueMenu.canRetryNow
            text: "Reintentar ahora"
            glyph: Theme.icon.retry
            onTriggered: page.downloads.retryWaiting(queueMenu.slug)
        }
        MenuEntry {
            id: nowEntry
            property bool wanted: !queueMenu.first
            text: "Descargar ahora"
            glyph: Theme.icon.download
            onTriggered: page.downloads.prioritize(queueMenu.slug)
        }
        MenuEntry {
            id: holdEntry
            property bool wanted: queueMenu.holdable
            text: "Pausar"
            glyph: Theme.icon.pause
            onTriggered: page.downloads.pauseSeries(queueMenu.slug)
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
            text: page.cancelScope(queueMenu.cancelCount) + (queueMenu.consequence !== "" ? "…" : "")
            glyph: Theme.icon.cancel
            onTriggered: page.askCancel(queueMenu.slug, queueMenu.name, queueMenu.consequence,
                                        queueMenu.cancelCount)
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
        property bool filesGone

        entries: [againEntry, completedAnimeEntry, completedRemoveEntry]

        function showFor(row, anchor) {
            slug = row.slug
            name = row.title
            files = row.filesOnDisk
            diskText = row.diskText
            filesGone = row.filesGone
            completedMenu.show(anchor, true)
        }

        MenuEntry {
            id: againEntry
            property bool wanted: completedMenu.filesGone
            text: "Volver a descargar"
            glyph: Theme.icon.download
            onTriggered: page.downloads.downloadAgain(completedMenu.slug)
        }
        MenuEntry {
            id: completedAnimeEntry
            text: "Ir al anime"
            glyph: Theme.icon.library
            onTriggered: page.openAnime(completedMenu.slug)
        }
        MenuEntry {
            id: completedRemoveEntry
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
        property string action: "Cancelar la descarga"

        width: Math.min(Math.max(460, implicitHeaderWidth), parent.width - 48)
        onOpened: cancelPrimary.forceActiveFocus(InputModality.reason)

        Label {
            width: cancelDialog.availableWidth
            text: cancelDialog.consequence
            color: Theme.ink
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
        }

        footer: DialogButtonBox {
            AccentButton {
                id: cancelPrimary
                text: cancelDialog.action
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                id: keepDownloadingButton
                text: "Seguir descargando"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Accessible.onPressAction: keepDownloadingButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    keepDownloadingButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    keepDownloadingButton.click()
                }
            }
        }

        onAccepted: taskId >= 0 ? page.downloads.cancel(taskId) : page.downloads.cancelSeries(slug)
    }

    ModalDialog {
        id: removeDialog

        property string slug
        property string consequence

        width: Math.min(Math.max(520, implicitHeaderWidth), parent.width - 48)
        onOpened: removePrimary.forceActiveFocus(InputModality.reason)

        Label {
            width: removeDialog.availableWidth
            text: removeDialog.consequence
            color: Theme.ink
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
        }

        footer: DialogButtonBox {
            AccentButton {
                id: removePrimary
                text: "Quitar de la lista"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                id: trashSeriesButton
                text: "Mover a la Papelera"
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                Accessible.onPressAction: trashSeriesButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    trashSeriesButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    trashSeriesButton.click()
                }
                onClicked: {
                    removeDialog.close()
                    page.downloads.trashSeries(removeDialog.slug)
                }
            }
            Button {
                id: removeCancelButton
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Accessible.onPressAction: removeCancelButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    removeCancelButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    removeCancelButton.click()
                }
            }
        }

        onAccepted: page.downloads.forgetSeries(slug)
    }

    ModalDialog {
        id: clearCompletedDialog

        width: Math.min(480, parent ? parent.width - 48 : 480)
        onAboutToShow: title = page.downloads.completed.count === 1
                               ? "¿Quitar 1 anime de Completadas?"
                               : "¿Quitar " + page.downloads.completed.count + " animes de Completadas?"
        onOpened: clearCompletedPrimary.forceActiveFocus(InputModality.reason)

        Label {
            width: Math.max(0, clearCompletedDialog.width - clearCompletedDialog.leftPadding
                               - clearCompletedDialog.rightPadding)
            text: "Se vaciará la lista de Completadas. Los archivos descargados seguirán intactos en el disco."
            color: Theme.ink
            font.pixelSize: Theme.body
            wrapMode: Text.Wrap
        }

        footer: DialogButtonBox {
            AccentButton {
                id: clearCompletedPrimary
                text: "Quitar todas"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                id: clearCompletedCancelButton
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Accessible.onPressAction: clearCompletedCancelButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    clearCompletedCancelButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    clearCompletedCancelButton.click()
                }
            }
        }

        onAccepted: page.downloads.clearCompleted()
    }
}
