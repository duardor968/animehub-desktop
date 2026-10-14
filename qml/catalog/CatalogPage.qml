import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// What AnimeHub has: news first, then the whole catalog with search and filters. Opening an
// anime pushes its page; Esc or the back button returns to the same scroll position.
StackView {
    id: stack

    required property AppController app
    property bool active: false

    readonly property CatalogView catalog: app.catalog

    onActiveChanged: if (active) catalog.start()

    // Back to the catalog itself, from any anime opened in it.
    function home() { pop(null) }

    function openAnime(slug) {
        app.openCatalogAnime(slug)
        if (depth === 1)
            push(animePage)
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

        RowLayout {
            id: toolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            anchors.leftMargin: 32
            spacing: 8

            TextField {
                id: search
                Layout.fillWidth: true
                Layout.minimumWidth: 160
                Layout.maximumWidth: 360
                placeholderText: "Buscar en el catálogo"
                text: stack.catalog.search
                onTextEdited: stack.catalog.search = text
                Accessible.name: "Buscar en el catálogo"
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

            // The shelves above resize with the window: whoever was at the top stays at the top.
            property real lastOriginY: 0
            onOriginYChanged: {
                if (contentY <= lastOriginY + 1)
                    positionViewAtBeginning()
                lastOriginY = originY
            }

            // The same posters as the shelves: their width, their gap, left of the scroll bar.
            readonly property real span: width - 16
            cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
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

                Shelf {
                    width: parent.width
                    visible: !stack.catalog.browsing && count > 0
                    readonly property int count: model ? model.count : 0
                    title: "Recién añadidos"
                    model: stack.catalog.recentAnime
                    onActivated: slug => stack.openAnime(slug)
                }

                Label {
                    text: stack.catalog.browsing
                          ? (stack.catalog.loading && grid.count === 0 ? "Buscando…"
                             : stack.catalog.capped ? "Más de " + stack.catalog.total + " resultados"
                             : stack.catalog.total === 1 ? "1 resultado"
                             : stack.catalog.total + " resultados")
                          : "Todo el catálogo"
                    color: Theme.ink
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                }
            }

            delegate: Item {
                required property var model
                width: grid.cellWidth
                height: grid.cellHeight

                PosterCard {
                    width: parent.width - Theme.posterGap
                    text: model.title
                    posterUrl: model.posterUrl
                    caption: model.caption
                    onClicked: stack.openAnime(model.slug)
                }
            }

            footer: Item {
                width: grid.width
                height: 72

                BusyIndicator {
                    anchors.centerIn: parent
                    running: stack.catalog.loading
                    visible: running
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: stack.catalog.problem !== ""
                    spacing: 8

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: stack.catalog.problem
                        color: Theme.ink
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Reintentar"
                        onClicked: stack.catalog.reload()
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: stack.catalog.browsing && !stack.catalog.loading
                             && stack.catalog.problem === "" && grid.count === 0
                    text: "Nada coincide con la búsqueda"
                    color: Theme.muted
                }
            }
        }

        Shortcut {
            sequences: [StandardKey.Find]
            enabled: stack.active && stack.depth === 1
            onActivated: search.forceActiveFocus()
        }
    }

    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        enabled: stack.active && stack.depth > 1
        onActivated: stack.pop()
    }
}
