import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// What AnimeHub has: the episodes added last, then the whole catalog with search, filters and
// its own order. Opening an anime pushes its page; Esc or the back button returns to the same
// scroll position. While something loads, its place already has its shape; a refresh leaves
// what was on view, dimmed, until the new results arrive.
StackView {
    id: stack

    required property AppController app
    property bool active: false

    readonly property CatalogView catalog: app.catalog

    onActiveChanged: if (active) catalog.start()

    // Back to the catalog itself, from any anime opened in it.
    function home() {
        if (depth > 1)
            pop(null)
    }

    function openAnime(slug) {
        app.openCatalogAnime(slug)
        if (depth === 1)
            push(animePage)
    }

    // The whole catalog searched for `text` (from the library's search).
    function search(text) {
        home()
        catalog.searchFor(text)
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
            backText: "Catálogo"
            onBack: stack.pop()
        }
    }

    initialItem: Item {
        id: browse

        function focusSearch() { search.forceActiveFocus() }

        RowLayout {
            id: toolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            anchors.leftMargin: 32
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
                Layout.preferredWidth: 180
                Layout.minimumWidth: 130
                model: ["Cualquier estado", "En emisión", "Finalizado", "Próximamente"]
                currentIndex: stack.catalog.status
                onActivated: index => stack.catalog.status = index
                Accessible.name: "Estado"
            }

            ComboBox {
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
                visible: stack.catalog.browsing
                flat: true
                text: "Quitar filtros"
                onClicked: stack.catalog.clearFilters()
            }

            Item { Layout.fillWidth: true }
        }

        GridView {
            id: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: toolbar.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 8
            anchors.leftMargin: 32
            anchors.rightMargin: 16
            clip: true
            model: stack.catalog.results
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            keyNavigationEnabled: true
            Accessible.name: "Catálogo"

            // The shelf above resizes with the window: whoever was at the top stays at the top.
            property real lastOriginY: 0
            onOriginYChanged: {
                if (contentY <= lastOriginY + 1)
                    positionViewAtBeginning()
                lastOriginY = originY
            }

            // The same posters as the shelves: their width, their gap, left of the scroll bar.
            readonly property real span: width - 16
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
                width: grid.span
                topPadding: 12
                bottomPadding: 16
                spacing: 32

                Shelf {
                    width: parent.width
                    visible: !stack.catalog.browsing && count > 0
                    readonly property int count: model ? model.count : 0
                    title: "Episodios recientes"
                    model: stack.catalog.recentEpisodes
                    onActivated: slug => stack.openAnime(slug)
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

                    Label {
                        text: "Episodios recientes"
                        color: Theme.ink
                        font.pixelSize: Theme.subtitle
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
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

                    // The order is this section's own, so it sits on its line.
                    RowLayout {
                        width: parent.width
                        spacing: 12

                        Label {
                            text: stack.catalog.browsing ? "Resultados" : "Todo el catálogo"
                            color: Theme.ink
                            font.pixelSize: Theme.subtitle
                            font.weight: Font.DemiBold
                            Accessible.role: Accessible.Heading
                        }

                        Label {
                            visible: text !== ""
                            text: stack.catalog.loading && stack.catalog.refreshing
                                  ? (stack.catalog.browsing ? "Buscando…" : "")
                                  : grid.count === 0 && !stack.catalog.browsing ? ""
                                  : stack.catalog.capped ? "(más de " + stack.catalog.total + ")"
                                  : "(" + stack.catalog.total + ")"
                            color: Theme.muted
                            font.pixelSize: Theme.subtitle
                        }

                        Item { Layout.fillWidth: true }

                        ComboBox {
                            Layout.preferredWidth: 180
                            Layout.minimumWidth: 130
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
                    RowLayout {
                        visible: grid.failed
                        spacing: 16

                        Label {
                            text: stack.catalog.problem
                            color: Theme.ink
                            font.pixelSize: Theme.body
                        }
                        Button {
                            text: "Reintentar"
                            onClicked: stack.catalog.reload()
                        }
                    }

                    RowLayout {
                        visible: grid.empty
                        spacing: 16

                        Label {
                            text: !stack.catalog.browsing ? "El catálogo no tiene animes todavía."
                                  : stack.catalog.search.trim() === "" ? "Ningún anime coincide con los filtros."
                                  : stack.catalog.status === 0 && stack.catalog.genre === ""
                                    ? "Ningún anime coincide con «" + stack.catalog.search.trim() + "»."
                                    : "Ningún anime coincide con «" + stack.catalog.search.trim() + "» y los filtros."
                            color: Theme.ink
                            font.pixelSize: Theme.body
                        }
                        Button {
                            text: stack.catalog.browsing ? "Quitar filtros" : "Reintentar"
                            onClicked: stack.catalog.browsing ? stack.catalog.clearFilters()
                                                              : stack.catalog.reload()
                        }
                    }
                }
            }

            delegate: Item {
                required property var model
                width: grid.cellWidth
                height: grid.cellHeight
                // The previous results stay, dimmed, until the new ones arrive.
                opacity: stack.catalog.refreshing ? 0.5 : 1
                Behavior on opacity { NumberAnimation { duration: 150 } }

                PosterCard {
                    width: parent.width - Theme.posterGap
                    text: model.title
                    posterUrl: model.posterUrl
                    caption: model.caption
                    owned: model.owned
                    onClicked: stack.openAnime(model.slug)
                }
            }

            footer: Item {
                width: grid.width
                height: grid.waiting ? shape.height : 72

                // The first results on their way.
                Grid {
                    id: shape
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
                    anchors.centerIn: parent
                    visible: stack.catalog.problemBelow
                    spacing: 8

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: stack.catalog.problem
                        color: Theme.ink
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Reintentar"
                        onClicked: stack.catalog.loadMore()
                    }
                }
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        enabled: stack.active && stack.depth > 1
        // As on the anime's own page, a selection of episodes is emptied first.
        onActivated: stack.currentItem.selecting ? stack.currentItem.clearSelection() : stack.pop()
    }
}
