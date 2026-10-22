import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library seen as posters: the anime watched last in large, what goes on, what has news,
// what airs next, and everything the list filter lets through.
Item {
    id: root

    required property AppController app
    // Why the filter leaves nothing.
    property string emptyText

    readonly property LibraryModel library: app.library
    readonly property bool hasFeatured: library.viewMode === LibraryModel.AllAnime && !!library.featured.slug
    readonly property string gridTitle: library.archivedOnly ? "Archivo" : ["Toda la biblioteca", "Viendo", "Sin empezar", "Vistos",
                                         "Favoritos", "Descargados"][library.filter]

    signal activated(string slug)
    signal franchiseActivated(var id)
    signal browseCatalog()

    // The backdrop of the featured anime reaches the pane's edges, beyond the grid's margins.
    clip: true

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Theme.section
        anchors.rightMargin: Theme.pad
        visible: count > 0
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
                visible: root.library.viewMode === LibraryModel.AllAnime && !root.library.archivedOnly && root.library.continueShelf.count > 0
                title: "Continuar viendo"
                model: root.library.continueShelf
                onActivated: slug => root.activated(slug)
            }

            Shelf {
                width: parent.width
                visible: root.library.viewMode === LibraryModel.AllAnime && !root.library.archivedOnly && root.library.freshShelf.count > 0
                title: "Episodios nuevos"
                model: root.library.freshShelf
                onActivated: slug => root.activated(slug)
            }

            Shelf {
                width: parent.width
                visible: root.library.viewMode === LibraryModel.AllAnime && !root.library.archivedOnly && root.library.upcomingShelf.count > 0
                title: "Próximos episodios"
                model: root.library.upcomingShelf
                onActivated: slug => root.activated(slug)
            }

            Label {
                text: root.gridTitle
                color: Theme.ink
                font.pixelSize: Theme.subtitle
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
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
        visible: grid.count === 0
        spacing: Theme.pad

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.library.search !== "" ? "Nada coincide con la búsqueda"
                  : root.library.total === 0 ? (root.library.archivedOnly ? "El Archivo está vacío" : "Tu biblioteca está vacía")
                  : root.library.viewMode === LibraryModel.Franchises ? "No hay franquicias en esta vista" : root.emptyText
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
                    root.library.viewMode = LibraryModel.Collection
                }
            }
        }
    }
}
