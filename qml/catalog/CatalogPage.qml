import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// What AnimeHub has: the episodes added last, then the whole catalog with search, filters and
// its own order. Opening an anime pushes its page; Esc, the breadcrumb or the back button returns
// to the same scroll position. While something loads, its place already has its shape; a refresh
// leaves what was on view, dimmed, until the new results arrive.
StackView {
    id: stack

    required property AppController app
    property bool active: false
    // Where the anime on view was opened from: "catalog", "schedule", or "downloads".
    property string from: "catalog"

    readonly property CatalogView catalog: app.catalog
    readonly property bool selectionBarVisible: depth > 1 && currentItem !== null
                                                && Boolean(currentItem.selecting)

    // Esc on an anime: the window goes back as its ← does.
    signal back()
    signal showDownloads()
    signal showSchedule()

    // Crisp fast transitions avoid Fluent's long default push/pop slide that can leave a blank
    // frame on rapid back-and-reopen sequences.
    pushEnter: Transition {
        PropertyAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }
    pushExit: Transition {
        PropertyAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }
    popEnter: Transition {
        PropertyAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }
    popExit: Transition {
        PropertyAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }

    onActiveChanged: if (active) catalog.start()

    // Back to the catalog itself, from any anime opened in it.
    function home() {
        if (depth > 1)
            pop(null)
    }

    // Unless `quiet` (← and →), the keyboard goes to the anime's title, framed when the keyboard
    // opened it.
    function openAnime(slug, origin, quiet) {
        const reason = ApplicationWindow.window ? ApplicationWindow.window.openingReason()
                                                : Qt.OtherFocusReason
        from = origin || "catalog"
        app.openCatalogAnime(slug)
        if (depth === 1)
            push(animePage)
        if (currentItem)
            currentItem.opacity = 1
        if (!quiet)
            focusTitle(reason)
    }

    function focusTitle(reason) {
        if (depth > 1)
            currentItem.focusTitle(reason)
    }

    // The whole catalog searched for `text` (from the library's search).
    function search(text) {
        home()
        catalog.searchFor(text)
    }

    // Pop to catalog browse and filter by a genre chip's slug (from AnimePage in Catalog or Library).
    function filterByGenre(slug) {
        home()
        catalog.start()
        if (catalog.search !== "" || catalog.status !== 0)
            catalog.clearFilters()
        if (catalog.genre !== slug)
            catalog.genre = slug
        else
            catalog.reload()
        browse.scrollToTop()
    }

    function focusSearch() {
        home()
        browse.focusSearch()
    }

    // A card before its data: the poster, two lines of title and the caption, as PosterCard.
    component SkeletonCard: Item {
        implicitHeight: Theme.posterCardHeight(width)

        Rectangle {
            id: cover
            width: parent.width
            height: Math.round(width * 1.42)
            color: Theme.raised
        }
        Rectangle {
            id: firstLine
            anchors.top: cover.bottom
            anchors.topMargin: 12
            width: parent.width * 0.85
            height: 12
            radius: 3
            color: Theme.raised
        }
        Rectangle {
            id: secondLine
            anchors.top: firstLine.bottom
            anchors.topMargin: 8
            width: parent.width * 0.55
            height: 12
            radius: 3
            color: Theme.raised
        }
        Rectangle {
            anchors.top: secondLine.bottom
            anchors.topMargin: 14
            width: parent.width * 0.35
            height: 10
            radius: 3
            color: Theme.raised
        }
    }

    // Breathes while it waits, all of it at once.
    component Waiting: SequentialAnimation {
        loops: Animation.Infinite
        NumberAnimation { to: 0.55; duration: 700; easing.type: Easing.InOutSine }
        NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
    }

    Component {
        id: animePage

        AnimePage {
            view: stack.app.catalogAnime
            steps: stack.from === "downloads"
                   ? [{ text: "Descargas", go: () => stack.showDownloads() }]
                   : stack.from === "schedule"
                   ? [{ text: "Horario", go: () => {
                           const win = stack.ApplicationWindow.window
                           if (win && typeof win.showPage === "function")
                               win.showPage(2)
                           stack.showSchedule()
                       } }]
                   : [{ text: "Catálogo", go: () => stack.home() }]
            onBack: stack.back()
            onGenreSelected: slug => stack.filterByGenre(slug)
        }
    }

    initialItem: Item {
        id: browse

        function focusSearch() { search.forceActiveFocus(InputModality.reason) }
        function scrollToTop() { grid.positionViewAtBeginning() }

        function syncFilters() {
            status.currentIndex = stack.catalog.status
            genreBox.currentIndex = Math.max(0, genreBox.indexOfValue(stack.catalog.genre))
        }

        Connections {
            target: stack.catalog
            function onQueryChanged() { browse.syncFilters() }
            function onGenresChanged() { browse.syncFilters() }
        }

        RowLayout {
            id: toolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.pad
            anchors.leftMargin: Theme.section
            spacing: 8

            SearchBox {
                id: search
                Layout.fillWidth: true
                Layout.minimumWidth: 160
                Layout.maximumWidth: 360
                placeholderText: "Buscar en el catálogo"
                text: stack.catalog.search
                onTextChanged: stack.catalog.search = text
            }

            ComboBox {
                id: status
                // Fits its longest option; AnimeAV1's "próximamente" also holds series between
                // seasons.
                Layout.preferredWidth: 230
                Layout.minimumWidth: 130
                model: ["Cualquier estado", "En emisión", "Finalizado", "Próximamente o en pausa"]
                currentIndex: stack.catalog.status
                onActivated: index => stack.catalog.status = index
                Accessible.name: "Estado"
            }

            ComboBox {
                id: genreBox
                Layout.preferredWidth: 180
                Layout.minimumWidth: 130
                textRole: "name"
                valueRole: "slug"
                model: [{ slug: "", name: "Todos los géneros" }].concat(stack.catalog.genres)
                currentIndex: Math.max(0, indexOfValue(stack.catalog.genre))
                onActivated: stack.catalog.genre = currentValue
                Accessible.name: "Género"
            }

            Button {
                id: clearFiltersButton
                visible: stack.catalog.browsing
                flat: true
                text: "Quitar filtros"
                Accessible.onPressAction: clearFiltersButton.click()
                onClicked: stack.catalog.clearFilters()
            }

            Item { Layout.fillWidth: true }
        }

        GridView {
            id: grid
            // 4 px left room inside the clip lets FocusFrame on column 0 and Shelf draw whole
            // while keeping the visual left edge aligned at Theme.section.
            readonly property int focusPad: 4

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: toolbar.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 8
            anchors.leftMargin: Theme.section - focusPad
            anchors.rightMargin: Theme.pad
            clip: true
            model: stack.catalog.results
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            keyNavigationEnabled: true
            // One stop for the keyboard, walked with the arrows; a card reached so comes into view.
            activeFocusOnTab: true
            highlightFollowsCurrentItem: false
            Accessible.name: "Catálogo"

            // The shelf above resizes with the window: whoever was at the top stays at the top.
            property real lastOriginY: 0
            onOriginYChanged: {
                if (contentY <= lastOriginY + 1)
                    positionViewAtBeginning()
                lastOriginY = originY
            }

            // The same posters as the shelves: their width, their gap, left of the scroll bar.
            readonly property real span: width - Theme.pad - focusPad
            readonly property int columns: Theme.posterColumns(span)
            // Nothing on view yet: the grid shows its shape instead.
            readonly property bool waiting: count === 0 && stack.catalog.loading
            // The first page failed: the reason above what stayed on view.
            readonly property bool failed: stack.catalog.problem !== "" && !stack.catalog.problemBelow
            readonly property bool empty: count === 0 && !stack.catalog.loading
                                          && !stack.catalog.refreshing && stack.catalog.problem === ""
            cellWidth: (span + Theme.posterGap) / columns
            cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap)
            onAtYEndChanged: if (atYEnd) stack.catalog.loadMore()

            header: Column {
                x: grid.focusPad
                width: grid.span
                topPadding: 12
                bottomPadding: Theme.pad
                spacing: Theme.section

                Shelf {
                    width: parent.width
                    visible: !stack.catalog.browsing && count > 0
                    count: model ? model.count : 0
                    title: "Episodios recientes"
                    model: stack.catalog.recentEpisodes
                    onActivated: slug => stack.openAnime(slug, "catalog")
                }

                // The recent episodes on their way.
                Column {
                    id: shelfShape
                    width: parent.width
                    visible: !stack.catalog.browsing && stack.catalog.homeLoading
                             && stack.catalog.recentEpisodes.count === 0
                    spacing: 12

                    readonly property int columns: Theme.posterColumns(width)
                    readonly property real cardWidth: (width - (columns - 1) * Theme.posterGap) / columns

                    Waiting on opacity { running: shelfShape.visible }

                    RowLayout {
                        width: parent.width
                        spacing: 0

                        Label {
                            text: "Episodios recientes"
                            color: Theme.ink
                            font.pixelSize: Theme.subtitle
                            font.weight: Font.DemiBold
                            Accessible.role: Accessible.Heading
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            implicitHeight: 1
                            color: Theme.stroke
                        }
                    }

                    Row {
                        spacing: Theme.posterGap

                        Repeater {
                            model: shelfShape.visible ? shelfShape.columns : 0

                            SkeletonCard { width: shelfShape.cardWidth }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 12

                    // As the Downloads headings: the count 8 px after the name, the line 12 px
                    // after the count. The order is this section's own, so it ends the line;
                    // when nothing came, there is nothing to order.
                    RowLayout {
                        width: parent.width
                        spacing: 0

                        Label {
                            text: stack.catalog.browsing ? "Resultados" : "Todo el catálogo"
                            color: Theme.ink
                            font.pixelSize: Theme.subtitle
                            font.weight: Font.DemiBold
                            Accessible.role: Accessible.Heading
                        }

                        Label {
                            Layout.leftMargin: 8
                            visible: text !== ""
                            text: stack.catalog.loading && stack.catalog.refreshing
                                  ? (stack.catalog.browsing ? "Buscando…" : "")
                                  : grid.count === 0 && !stack.catalog.browsing ? ""
                                  : stack.catalog.capped ? "(más de " + stack.catalog.total + ")"
                                  : "(" + stack.catalog.total + ")"
                            color: Theme.muted
                            font.pixelSize: Theme.subtitle
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            implicitHeight: 1
                            color: Theme.stroke
                        }

                        ComboBox {
                            Layout.leftMargin: 12
                            Layout.preferredWidth: 180
                            Layout.minimumWidth: 130
                            visible: !(grid.failed && grid.count === 0)
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: "Recién añadidos", value: 4 },
                                { text: "Último episodio", value: 5 },
                                { text: "Mejor puntuados", value: 1 },
                                { text: "Más populares", value: 2 },
                                { text: "Por título", value: 3 }
                            ]
                            currentIndex: Math.max(0, indexOfValue(stack.catalog.order))
                            onActivated: stack.catalog.order = currentValue
                            Accessible.name: "Orden"
                        }
                    }

                    // Why nothing new came, and the way out; what was on view stays under it.
                    Rectangle {
                        visible: grid.failed
                        width: parent.width
                        implicitHeight: failedRow.implicitHeight + 20
                        radius: Theme.radius
                        color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.10)
                        border.width: 1
                        border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.30)

                        RowLayout {
                            id: failedRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Glyph {
                                glyph: Theme.icon.warning
                                size: 14
                                color: Theme.danger
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: stack.catalog.problem
                                color: Theme.ink
                                font.pixelSize: Theme.body
                                wrapMode: Text.Wrap
                            }

                            Button {
                                id: retryFailedButton
                                text: "Reintentar"
                                Accessible.onPressAction: retryFailedButton.click()
                                onClicked: stack.catalog.reload()
                            }
                        }
                    }

                    RowLayout {
                        visible: grid.empty
                        width: parent.width
                        spacing: Theme.pad

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: !stack.catalog.browsing ? "El catálogo no tiene animes todavía"
                                  : stack.catalog.search.trim() === "" ? "Ningún anime coincide con los filtros"
                                  : stack.catalog.status === 0 && stack.catalog.genre === ""
                                    ? "Ningún anime coincide con «" + stack.catalog.search.trim() + "»"
                                    : "Ningún anime coincide con «" + stack.catalog.search.trim() + "» y los filtros"
                            color: Theme.muted
                            font.pixelSize: Theme.body
                            wrapMode: Text.Wrap
                        }
                        // While searching, "Quitar filtros" is already in the toolbar.
                        Button {
                            id: retryEmptyButton
                            visible: !stack.catalog.browsing
                            text: "Reintentar"
                            Accessible.onPressAction: retryEmptyButton.click()
                            onClicked: stack.catalog.reload()
                        }
                    }
                }
            }

            // A scope, so the card inside takes the keyboard when the grid makes it current.
            delegate: FocusScope {
                required property var model
                required property int index
                width: grid.cellWidth
                height: grid.cellHeight
                // The previous results stay, dimmed, until the new ones arrive.
                opacity: stack.catalog.refreshing ? 0.5 : 1
                Behavior on opacity { NumberAnimation { duration: 150 } }

                PosterCard {
                    focus: true
                    x: grid.focusPad
                    // The grid is the keyboard's stop, not each card.
                    focusPolicy: Qt.ClickFocus
                    width: parent.width - Theme.posterGap
                    text: model.title
                    posterUrl: model.posterUrl
                    caption: model.caption
                    owned: model.owned
                    onClicked: {
                        grid.currentIndex = index
                        stack.openAnime(model.slug, "catalog")
                    }
                }
            }

            footer: Item {
                width: grid.width
                height: grid.waiting ? shape.height
                        : stack.catalog.problemBelow ? Math.max(72, problemBelowCol.implicitHeight + 24)
                        : 72

                // The first results on their way.
                Grid {
                    id: shape
                    x: grid.focusPad
                    visible: grid.waiting
                    columns: grid.columns

                    Waiting on opacity { running: shape.visible }

                    Repeater {
                        model: grid.waiting ? 2 * grid.columns : 0

                        Item {
                            width: grid.cellWidth
                            height: grid.cellHeight

                            SkeletonCard { width: parent.width - Theme.posterGap }
                        }
                    }
                }

                // More of the same results on their way.
                BusyIndicator {
                    anchors.centerIn: parent
                    running: stack.catalog.loading && !grid.waiting && !stack.catalog.refreshing
                    visible: running
                }

                // The next page failed: the reason where the list stopped.
                ColumnLayout {
                    id: problemBelowCol
                    anchors.centerIn: parent
                    width: Math.min(Math.max(0, parent.width - Theme.section), 560)
                    visible: stack.catalog.problemBelow
                    spacing: 8

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: parent.width
                        spacing: 8

                        Glyph {
                            glyph: Theme.icon.warning
                            size: 14
                            color: Theme.danger
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: stack.catalog.problem
                            color: Theme.ink
                            font.pixelSize: Theme.body
                        }
                    }
                    Button {
                        id: retryMoreButton
                        Layout.alignment: Qt.AlignHCenter
                        text: "Reintentar"
                        Accessible.onPressAction: retryMoreButton.click()
                        onClicked: stack.catalog.loadMore()
                    }
                }
            }
        }
    }

    // Esc from anywhere in the tab, as on the anime's own page: a selection of episodes is
    // emptied first. Alt+← is the window's.
    Shortcut {
        sequence: "Esc"
        enabled: stack.active && stack.depth > 1
                 && (!stack.ApplicationWindow.window || stack.ApplicationWindow.window.modalLayers === 0)
        onActivated: stack.currentItem.selecting ? stack.currentItem.clearSelection() : stack.back()
    }
}
