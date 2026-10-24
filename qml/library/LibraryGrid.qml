import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// What the list shows, as posters: the same filter, grouping and order, to browse by sight.
// GridView draws only the rows on view, so a long library costs nothing.
Item {
    id: root

    required property LibraryModel library
    // Why the filter leaves nothing.
    property string emptyText

    readonly property string title: library.archivedOnly ? "Archivo" : ({
        [LibraryModel.Watching]: "Viendo",
        [LibraryModel.Airing]: "En emisión",
        [LibraryModel.NotStarted]: "Sin empezar",
        [LibraryModel.Watched]: "Vistos",
        [LibraryModel.Favorites]: "Favoritos"
    })[library.filter] ?? "Animes"

    signal activated(string slug)
    signal franchiseActivated(var id)

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Theme.section
        anchors.rightMargin: Theme.pad
        visible: count > 0
        model: root.library.overview
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: root.title

        // The same posters as the shelves: their width, their gap, left of the scroll bar.
        readonly property real span: width - Theme.pad
        cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
        cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap)

        header: RowLayout {
            width: grid.span
            height: 76
            spacing: 8

            Label {
                text: root.title
                color: Theme.ink
                font.pixelSize: Theme.subtitle
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }

            Label {
                text: "(" + grid.count + ")"
                color: Theme.muted
                font.pixelSize: Theme.subtitle
            }

            // As Steam's shelves: a hairline to the edge after the title.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.preferredHeight: 1
                color: Theme.stroke
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
                onClicked: modelData.groupId ? root.franchiseActivated(modelData.groupId)
                                             : root.activated(modelData.slug)
            }
        }
    }

    Label {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.section, 420)
        visible: !grid.visible
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: root.emptyText || "Nada que mostrar"
        color: Theme.ink
        font.pixelSize: Theme.subtitle
        font.weight: Font.DemiBold
    }
}
