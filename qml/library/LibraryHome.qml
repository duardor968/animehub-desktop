import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library seen as posters: the next episode to watch in large, what else is ready to watch
// and what to download, what airs next, and everything the list filter lets through.
Item {
    id: root

    required property AppController app
    // PROTOTYPE: "A" shows what to watch and to download here; "B" leaves it to the filter.
    property string variant: "A"
    // Why the filter leaves nothing.
    property string emptyText

    readonly property LibraryModel library: app.library
    // The whole library, not a part of it: the home leads with what to do next.
    readonly property bool whole: !library.archivedOnly && (library.filter === LibraryModel.All
                                                            || library.filter === LibraryModel.AllAnime
                                                            || library.filter === LibraryModel.Franchises)
    readonly property bool tasks: whole && variant === "A"
    readonly property bool hasFeatured: whole && !!library.featured.slug
    readonly property string gridTitle: library.archivedOnly ? "Archivo" : ({
        [LibraryModel.Franchises]: "Franquicias",
        [LibraryModel.ToWatch]: "Para ver",
        [LibraryModel.ToDownload]: "Para descargar",
        [LibraryModel.Airing]: "En emisión",
        [LibraryModel.Watching]: "Viendo",
        [LibraryModel.NotStarted]: "Sin empezar",
        [LibraryModel.Watched]: "Vistos",
        [LibraryModel.Favorites]: "Favoritos",
        [LibraryModel.OnDisk]: "En disco"
    })[library.filter] ?? "Toda la biblioteca"

    signal activated(string slug)
    signal franchiseActivated(var id)
    signal browseCatalog()

    // Large batches are confirmed first, as on the web; smaller ones go straight to the queue.
    function downloadAll() {
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

    // The backdrop of the featured anime reaches the pane's edges, beyond the grid's margins.
    clip: true

    ConfirmDialog { id: confirm }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Theme.section
        anchors.rightMargin: Theme.pad
        visible: count > 0 || root.tasks && (root.library.readyCount + root.library.downloadCount) > 0
        clip: false
        model: root.library.overview
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: root.gridTitle

        // The shelves above resize with the window: whoever was at the top stays at the top.
        property real lastOriginY: 0
        onOriginYChanged: {
            if (contentY <= lastOriginY + 1)
                positionViewAtBeginning()
            lastOriginY = originY
        }

        // The same posters as the shelves: their width, their gap, left of the scroll bar.
        readonly property real span: width - Theme.pad
        cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
        cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap)

        header: Column {
            width: grid.span
            topPadding: root.hasFeatured ? 0 : 28
            spacing: Theme.section
            bottomPadding: Theme.pad

            FeaturedAnime {
                x: -grid.x
                width: root.width
                visible: root.hasFeatured
                inset: grid.x
                featured: root.library.featured
                onOpen: slug => root.activated(slug)
                onAct: slug => root.app.continueWatching(slug)
            }

            Shelf {
                width: parent.width
                visible: root.tasks && root.library.readyShelf.count > 0
                title: "Para ver"
                model: root.library.readyShelf
                onActivated: slug => root.activated(slug)
            }

            Shelf {
                width: parent.width
                visible: root.tasks && root.library.downloadShelf.count > 0
                title: "Para descargar"
                count: root.library.downloadCount
                model: root.library.downloadShelf
                onActivated: slug => root.activated(slug)

                Button {
                    text: root.library.pendingCount === 1 ? "Descargar 1 episodio"
                                                          : "Descargar " + root.library.pendingCount + " episodios"
                    icon.source: Theme.source(Theme.icon.download)
                    onClicked: root.downloadAll()
                }
            }

            Shelf {
                width: parent.width
                visible: root.whole && root.library.upcomingShelf.count > 0
                title: "Próximos episodios"
                model: root.library.upcomingShelf
                onActivated: slug => root.activated(slug)
            }

            RowLayout {
                width: parent.width
                spacing: 12

                Label {
                    text: root.gridTitle
                    color: Theme.ink
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                }

                Label {
                    visible: !root.whole
                    text: "(" + grid.count + ")"
                    color: Theme.muted
                    font.pixelSize: Theme.subtitle
                }

                Button {
                    Layout.leftMargin: 12
                    visible: root.library.filter === LibraryModel.ToDownload && !root.library.archivedOnly
                             && root.library.pendingCount > 0
                    text: root.library.pendingCount === 1 ? "Descargar 1 episodio"
                                                          : "Descargar " + root.library.pendingCount + " episodios"
                    icon.source: Theme.source(Theme.icon.download)
                    onClicked: root.downloadAll()
                }

                Item { Layout.fillWidth: true }
            }
        }

        delegate: Item {
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight

            PosterCard {
                width: parent.width - Theme.posterGap
                text: modelData.title
                posterUrl: modelData.posterUrl
                caption: modelData.caption
                progress: modelData.progress
                fresh: false
                onClicked: modelData.groupId ? root.franchiseActivated(modelData.groupId) : root.activated(modelData.slug)
            }
        }
    }

    // Nothing to show: why, and the way out.
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.section, 420)
        visible: !grid.visible
        spacing: Theme.pad

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.library.search !== "" ? "Nada coincide con la búsqueda"
                  : root.library.total === 0 ? (root.library.archivedOnly ? "El Archivo está vacío" : "Tu biblioteca está vacía")
                  : root.emptyText
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: root.library.archivedOnly ? "Volver a la biblioteca" : root.library.total === 0 ? "Explorar el catálogo" : "Mostrar todo"
            highlighted: true
            onClicked: {
                if (root.library.archivedOnly)
                    root.library.archivedOnly = false
                else if (root.library.total === 0)
                    root.browseCatalog()
                else {
                    root.library.search = ""
                    root.library.filter = LibraryModel.All
                }
            }
        }
    }
}
