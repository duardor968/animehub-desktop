import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One anime, as Steam shows a game: the header, the bar with the main action and the figures
// (it stays at the top once it gets there), the synopsis, the episodes in pages of fifty and,
// last, related anime.
Item {
    id: page

    required property AnimeView view

    // Esc leaves the page; the window's ← and → go anywhere it has been.
    signal back()

    // "Elegir episodios": every row shows its check box until the selection is used or emptied.
    property bool choosing: false
    readonly property bool selecting: choosing || view.episodes.selectedCount > 0
    // Rows and their heading stop at a width where their buttons stay near their text.
    readonly property real contentWidth: Math.min(list.width - 2 * Theme.section, 1100)

    function clearSelection() {
        view.episodes.clearSelection()
        choosing = false
    }

    function chooseEpisodes() {
        choosing = true
        scrollToEpisodes(true)
    }

    // The head of the episode list at the top of the view, under the bar.
    function scrollToEpisodes(animated) {
        const header = list.headerItem
        if (!header)
            return
        const bottom = list.originY + list.contentHeight - list.height
        const top = header.y + header.episodesY - actionBar.height - 12
        const target = Math.max(list.originY, Math.min(top, bottom))
        page.scrolled = true
        if (animated) {
            scroll.to = target
            scroll.restart()
        } else {
            list.contentY = target
        }
    }

    Keys.onEscapePressed: page.selecting ? page.clearSelection() : page.back()

    EpisodeMenu {
        id: episodeMenu
        view: page.view
    }

    ConfirmDialog {
        id: confirm
    }

    RangeDialog {
        id: rangeDialog
        view: page.view
    }

    // A new anime starts at its top; its header settles as images arrive, so keep it there
    // until the user scrolls.
    property bool scrolled: false
    Connections {
        target: page.view
        function onSlugChanged() {
            page.scrolled = false
            page.choosing = false
            list.positionViewAtBeginning()
        }
        function onDownloadConfirmationNeeded(numbers, question, detail, selection) {
            confirm.ask(question, detail, "Descargar", "Cancelar", () => {
                page.view.downloadEpisodes(numbers)
                if (selection)
                    page.clearSelection()
            })
        }
        function onCancelConfirmationNeeded(number, question, detail) {
            confirm.ask(question, detail, "Cancelar la descarga", "Seguir descargando", () => {
                if (number < 0)
                    page.view.cancelSeries(true)
                else
                    page.view.cancelDownload(number, true)
            })
        }
    }

    // Choosing ends once the chosen episodes have been acted on.
    property int chosen: 0
    Connections {
        target: page.view.episodes
        function onSelectionChanged() {
            const count = page.view.episodes.selectedCount
            if (count === 0 && page.chosen > 0)
                page.choosing = false
            page.chosen = count
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        onMovementStarted: page.scrolled = true
        onOriginYChanged: if (!page.scrolled) positionViewAtBeginning()
        clip: true
        // A film has no list: everything about it is above.
        model: page.view.single ? null : page.view.episodes
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        currentIndex: -1
        // Room for the selection bar under the last row.
        bottomMargin: page.selecting ? selectionBar.height + 24 : 0
        Accessible.name: "Episodios"

        NumberAnimation {
            id: scroll
            target: list
            property: "contentY"
            duration: 300
            easing.type: Easing.OutCubic
        }

        header: Column {
            id: overview

            readonly property real barY: barSlot.y
            readonly property real episodesY: episodesBar.y

            width: list.width

            AnimeHeader {
                width: parent.width
                title: page.view.title
                subtitle: page.view.alternativeTitle
                score: page.view.score
                facts: page.view.facts
                posterUrl: page.view.posterUrl
                backdropUrl: page.view.backdropUrl
            }

            // Where the bar sits until it reaches the top.
            Item {
                id: barSlot
                width: parent.width
                height: actionBar.height
            }

            ColumnLayout {
                x: Theme.section
                width: page.contentWidth
                spacing: 12

                // Room above the first line, whichever it is.
                Item {
                    Layout.preferredHeight: 12
                }

                RowLayout {
                    visible: page.view.problem !== ""
                    spacing: 10

                    Glyph {
                        glyph: Theme.icon.warning
                        color: Theme.warning
                    }

                    Label {
                        text: page.view.problem
                        color: Theme.ink
                    }

                    Button {
                        text: "Reintentar"
                        onClicked: page.view.reload()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: synopsis.Layout.maximumWidth
                    visible: text !== ""
                    text: page.view.genres
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    wrapMode: Text.Wrap
                }

                Label {
                    id: synopsis
                    property bool expanded: false
                    // Collapsed, the first paragraph: it usually ends a sentence, so no "….".
                    readonly property var paragraphs: page.view.synopsis.split(/\n\s*\n/)
                    // Each anime opens collapsed.
                    onParagraphsChanged: expanded = false
                    Layout.fillWidth: true
                    // About 75 characters a line: the measure prose reads best at.
                    Layout.maximumWidth: 720
                    visible: text !== ""
                    text: expanded ? page.view.synopsis : paragraphs[0]
                    color: Theme.ink
                    opacity: 0.88
                    font.pixelSize: Theme.body
                    lineHeight: 1.3
                    wrapMode: Text.Wrap
                    maximumLineCount: expanded ? 1000 : 4
                    elide: Text.ElideRight
                }

                Button {
                    visible: synopsis.expanded || synopsis.truncated || synopsis.paragraphs.length > 1
                    flat: true
                    text: synopsis.expanded ? "Mostrar menos" : "Mostrar más"
                    onClicked: synopsis.expanded = !synopsis.expanded
                    // Its text in the synopsis column; the hover background may overhang, as in WinUI.
                    Layout.leftMargin: -leftPadding
                    Layout.topMargin: -8
                }

                Item {
                    Layout.preferredHeight: 8
                }
            }

            EpisodesBar {
                id: episodesBar
                x: Theme.section
                width: page.contentWidth
                visible: !page.view.single && page.view.episodes.total > 0
                view: page.view
                selecting: page.selecting
                onMoved: page.scrollToEpisodes(false)
            }
        }

        delegate: EpisodeRow {
            // The list places its rows at its edge: the padding puts the content in the column, and
            // the hover surface overhangs it a little, as WinUI's list items do.
            width: list.width
            leftPadding: Theme.section
            rightPadding: list.width - Theme.section - page.contentWidth
            leftInset: Theme.section - 12
            rightInset: rightPadding - 12
            view: page.view
            menu: episodeMenu
            selecting: page.selecting
        }

        footer: Column {
            id: footerColumn

            readonly property AnimeView view: page.view

            width: list.width
            topPadding: 12
            bottomPadding: Theme.section
            spacing: Theme.section

            Item {
                width: parent.width
                height: bottomPager.visible ? bottomPager.implicitHeight : busy.height
                visible: bottomPager.visible || busy.running

                BusyIndicator {
                    id: busy
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: page.view.refreshing && page.view.episodes.total === 0
                    visible: running
                }

                Pager {
                    id: bottomPager
                    x: Theme.section + page.contentWidth - width
                    visible: !page.view.single && page.view.episodes.pageCount > 1
                    episodes: page.view.episodes
                    onMoved: page.scrollToEpisodes(false)
                }
            }

            Shelf {
                id: related
                x: Theme.section
                width: parent.width - 2 * Theme.section
                title: "Relacionados"
                // Not `page.view`: inside a Shelf, `page` is its own paging function.
                model: footerColumn.view.related
                onActivated: slug => {
                    // Main.qml opens it in the library when it is there, else in the catalog.
                    const window = ApplicationWindow.window
                    if (window && typeof window.openAnime === "function")
                        window.openAnime(slug)
                }
            }
        }
    }

    ActionBar {
        id: actionBar

        // Where the header leaves it, until it reaches the top.
        readonly property real slotY: {
            const header = list.headerItem
            return header ? header.y + header.barY - list.contentY : 0
        }

        anchors.left: parent.left
        anchors.right: parent.right
        y: Math.max(0, slotY)
        pinned: slotY < 0
        visible: list.headerItem !== null
        figures: page.view.figures

        PrimaryActions {
            view: page.view
            onChooseRange: rangeDialog.open()
            onChooseEpisodes: page.chooseEpisodes()
        }

        // Watching or downloading also adds it; this is for keeping it without either.
        Button {
            visible: page.view.loaded && !page.view.inLibrary
            text: "Añadir a la biblioteca"
            icon.source: Theme.source(Theme.icon.add)
            onClicked: page.view.setInLibrary(true)
        }

        trailing: [
            ToolButton {
                visible: page.view.loaded
                icon.source: Theme.source(page.view.favorite ? Theme.icon.starFilled : Theme.icon.star)
                icon.color: page.view.favorite ? Theme.warning : Theme.ink
                display: AbstractButton.IconOnly
                text: page.view.favorite ? "Quitar de favoritos" : "Añadir a favoritos"
                Accessible.checkable: true
                Accessible.checked: page.view.favorite
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: page.view.setFavorite(!page.view.favorite)
            },
            ToolButton {
                // Segoe's bell with a stroke through it.
                readonly property string bellOff: "e7ed"
                visible: page.view.airing && page.view.inLibrary
                icon.source: Theme.source(page.view.notify ? Theme.icon.bell : bellOff)
                display: AbstractButton.IconOnly
                text: page.view.notify ? "Dejar de avisar de episodios nuevos"
                                       : "Avisar de episodios nuevos"
                Accessible.checkable: true
                Accessible.checked: page.view.notify
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: page.view.setNotify(!page.view.notify)
            },
            ToolButton {
                id: moreButton
                visible: page.view.loaded
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                text: "Más opciones"
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: {
                    moreMenu.folderExists = page.view.hasFolder()
                    moreMenu.show(moreButton, true)
                }
            }
        ]
    }

    ActionMenu {
        id: moreMenu

        // Checked when the menu opens: the folder appears with the first download.
        property bool folderExists: false

        entries: [autoEntry, airingSeparator, watchedEntry, unwatchedEntry, positionEntry,
                  downloadsSeparator, pauseEntry, resumeEntry, retryEntry, cancelEntry, sendEntry,
                  filesSeparator, trashWatchedEntry, trashFilmEntry, folderEntry, trailerEntry,
                  librarySeparator, removeEntry]

        MenuEntry {
            id: autoEntry
            property bool wanted: page.view.airing
            text: "Descargar episodios nuevos"
            checkable: true
            checked: page.view.autoDownload
            onTriggered: {
                if (checked) {
                    checked = false // the user confirms first
                    autoDownloadDialog.open()
                } else {
                    page.view.setAutoDownload(false)
                }
            }
        }
        MenuSeparator {
            id: airingSeparator
            property bool separator: true
        }
        MenuEntry {
            id: watchedEntry
            property bool wanted: page.view.watchedProgress < 1
            text: page.view.single ? "Marcar como visto" : "Marcar todo como visto"
            onTriggered: page.view.markAllWatched()
        }
        MenuEntry {
            id: unwatchedEntry
            property bool wanted: page.view.started
            text: page.view.single ? "Marcar como no visto" : "Marcar todo como no visto"
            onTriggered: page.view.markAllUnwatched()
        }
        MenuEntry {
            id: positionEntry
            property bool wanted: page.view.single
            text: "Continuar desde…"
            glyph: Theme.icon.clock
            onTriggered: episodeMenu.askPosition(page.view.firstNumber, page.view.title)
        }
        MenuSeparator {
            id: downloadsSeparator
            property bool separator: true
        }
        // The series' downloads; the main button handles only the next episode's.
        MenuEntry {
            id: pauseEntry
            property bool wanted: !page.view.single && page.view.downloadRunning
            text: "Pausar las descargas"
            glyph: Theme.icon.pause
            onTriggered: page.view.pauseSeries()
        }
        MenuEntry {
            id: resumeEntry
            property bool wanted: !page.view.single && page.view.downloadPaused
            text: "Reanudar las descargas"
            glyph: Theme.icon.download
            onTriggered: page.view.resumeSeries()
        }
        MenuEntry {
            id: retryEntry
            property bool wanted: !page.view.single && page.view.failedCount > 0
            text: "Reintentar las fallidas"
            glyph: Theme.icon.retry
            onTriggered: page.view.retryFailed()
        }
        MenuEntry {
            id: cancelEntry
            property bool wanted: page.view.hasDownloads
            text: page.view.single ? "Cancelar la descarga" : "Cancelar las descargas"
            glyph: Theme.icon.cancel
            onTriggered: page.view.cancelSeries(false)
        }
        MenuEntry {
            id: sendEntry
            property bool wanted: page.view.single && page.view.canSendToJDownloader
                                  && page.view.onDiskCount === 0
            text: "Enviar a JDownloader"
            glyph: Theme.icon.send
            onTriggered: page.view.sendToJDownloader(page.view.firstNumber)
        }
        MenuSeparator {
            id: filesSeparator
            property bool separator: true
        }
        MenuEntry {
            id: trashWatchedEntry
            property bool wanted: page.view.watchedOnDisk && !page.view.single
            text: "Mover los vistos a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: page.view.trashWatched()
        }
        MenuEntry {
            id: trashFilmEntry
            property bool wanted: page.view.single && page.view.onDiskCount > 0
            text: "Mover a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: page.view.trashFile(page.view.firstNumber)
        }
        MenuEntry {
            id: folderEntry
            property bool wanted: moreMenu.folderExists
            text: "Abrir la carpeta"
            glyph: Theme.icon.folder
            onTriggered: page.view.openFolder()
        }
        MenuEntry {
            id: trailerEntry
            property bool wanted: page.view.trailerUrl.toString() !== ""
            text: "Ver el tráiler"
            // Segoe's "open in a new window": it plays in the browser.
            glyph: "e8a7"
            onTriggered: page.view.openTrailer()
        }
        MenuSeparator {
            id: librarySeparator
            property bool separator: true
        }
        MenuEntry {
            id: removeEntry
            property bool wanted: page.view.inLibrary
            text: "Quitar de la biblioteca…"
            glyph: Theme.icon.remove
            onTriggered: removalDialog.show([page.view.slug], page.view.title, false)
        }
    }

    LibraryRemovalDialog {
        id: removalDialog
        model: page.ApplicationWindow.window.app.library
    }

    ModalDialog {
        id: autoDownloadDialog
        width: 480
        title: "Descargar episodios nuevos"

        // Fixed widths on both sides: Fluent's Dialog loops when its content follows it.
        Label {
            width: 432
            wrapMode: Text.Wrap
            color: Theme.ink
            text: "Cuando salga un episodio nuevo de " + page.view.title
                  + ", AnimeHub lo descargará en " + page.view.downloadFolder
                  + ". Puedes desactivarlo en este mismo menú."
        }

        footer: DialogButtonBox {
            Button {
                text: "Activar"
                highlighted: autoDownloadDialog.visible // DialogButtonBox clears it while it builds
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: page.view.setAutoDownload(true)
    }

    SelectionBar {
        id: selectionBar
        anchors.horizontalCenter: parent.horizontalCenter
        // Rises from the bottom edge.
        y: page.selecting ? page.height - height - 16 : page.height + 8
        opacity: page.selecting ? 1 : 0
        visible: opacity > 0
        Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200 } }
        view: page.view
        available: page.width - 32
        onCleared: page.clearSelection()
    }
}
