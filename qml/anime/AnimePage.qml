import QtQuick
import QtQuick.Controls
import AnimeHub

// One anime: the header with its main action, the user's progress, related anime and the
// episodes in pages of fifty. A single scroll; once the header leaves, a compact bar keeps the
// main action at hand.
Item {
    id: page

    required property AnimeView view
    property string backText

    signal back()

    // "Elegir episodios": every row shows its check box until the selection is used or emptied.
    property bool choosing: false
    readonly property bool selecting: choosing || view.episodes.selectedCount > 0

    function clearSelection() {
        view.episodes.clearSelection()
        choosing = false
    }

    function chooseEpisodes() {
        choosing = true
        scrollToEpisodes(true)
    }

    // The head of the episode list at the top of the view, under the compact bar.
    function scrollToEpisodes(animated) {
        const header = list.headerItem
        if (!header)
            return
        const bottom = list.originY + list.contentHeight - list.height
        const top = header.y + header.episodesY - compactBar.height - 12
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

    // A new anime starts at its top; its header grows as images arrive, so keep it there until
    // the user scrolls.
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
        // A film has no list: everything about it is in the header.
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

            readonly property AnimeView view: page.view
            readonly property alias hero: hero
            readonly property real episodesY: episodesBar.y

            width: list.width
            spacing: Theme.section
            bottomPadding: 12

            AnimeHeader {
                id: hero
                width: parent.width
                view: page.view
                backText: page.backText
                onBack: page.back()
                onChooseRange: rangeDialog.open()
                onChooseEpisodes: page.chooseEpisodes()
                onAskPosition: episodeMenu.askPosition(page.view.firstNumber, page.view.title)
            }

            ProgressBand {
                x: 32
                width: parent.width - 64
                visible: page.view.showProgress
                view: page.view
            }

            Shelf {
                x: 32
                width: parent.width - 64
                title: "Relacionados"
                // Not `page.view`: inside a Shelf, `page` is its own paging function.
                model: overview.view.related
                onActivated: slug => {
                    // Main.qml opens it in the library when it is there, else in the catalog.
                    const window = ApplicationWindow.window
                    if (window && typeof window.openAnime === "function")
                        window.openAnime(slug)
                }
            }

            EpisodesBar {
                id: episodesBar
                x: 32
                width: parent.width - 64
                visible: !page.view.single && page.view.episodes.total > 0
                view: page.view
                onMoved: page.scrollToEpisodes(false)
            }
        }

        delegate: EpisodeRow {
            width: list.width
            view: page.view
            menu: episodeMenu
            selecting: page.selecting
        }

        footer: Item {
            width: list.width
            height: bottomPager.visible ? 80 : 48

            BusyIndicator {
                anchors.centerIn: parent
                running: page.view.refreshing && page.view.episodes.total === 0
                visible: running
            }

            Pager {
                id: bottomPager
                anchors.right: parent.right
                anchors.rightMargin: 32
                anchors.verticalCenter: parent.verticalCenter
                visible: !page.view.single && page.view.episodes.pageCount > 1
                episodes: page.view.episodes
                onMoved: page.scrollToEpisodes(false)
            }
        }
    }

    CompactBar {
        id: compactBar

        // Re-evaluated as the list scrolls: shown once the header's actions are above the view.
        readonly property bool wanted: {
            list.contentY
            const header = list.headerItem
            if (!header)
                return false
            const actions = header.hero.actions
            return actions.mapToItem(page, 0, actions.height).y < 0
        }

        anchors.left: parent.left
        anchors.right: parent.right
        y: wanted ? 0 : -height
        opacity: wanted ? 1 : 0
        visible: opacity > 0
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 180 } }
        view: page.view
        backText: page.backText
        onBack: page.back()
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
