import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// What the list shows, as posters: the same filter, grouping and order, to browse by sight, and
// counted as the list's filter counts them. GridView draws only the rows on view, so a long
// library costs nothing; for the keyboard it is one stop walked with the arrows.
Item {
    id: root

    required property LibraryModel library
    // Why the filter leaves nothing, how it fills, and the way out.
    property string emptyText
    property string emptyDetail
    property string emptyAction

    readonly property string title: library.archivedOnly ? "Archivo" : ({
        [LibraryModel.ToWatch]: "Para ver",
        [LibraryModel.Watching]: "Viendo",
        [LibraryModel.Airing]: "En emisión",
        [LibraryModel.NotStarted]: "Sin empezar",
        [LibraryModel.Watched]: "Vistos",
        [LibraryModel.Favorites]: "Favoritos"
    })[library.filter] ?? "Animes"

    signal activated(string slug)
    signal franchiseActivated(var id)
    signal emptyActed()

    GridView {
        id: grid
        anchors.fill: parent
        // Extra 4 px on the left inside the clip rect keeps column 0's 3 px FocusFrame unclipped
        // while content stays aligned at Theme.section.
        anchors.leftMargin: Theme.section - 4
        anchors.rightMargin: Theme.pad
        bottomMargin: 8
        visible: count > 0
        model: root.library.overview
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        activeFocusOnTab: true
        // A card reached by keyboard brings itself into view.
        highlightFollowsCurrentItem: false
        Accessible.name: root.title

        // The same posters as the shelves: their width, their gap, left of the scroll bar.
        readonly property real span: width - 4 - Theme.pad
        cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
        cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap)

        header: RowLayout {
            x: 4
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

            // Works, as the filter counts them: a franchise counts its works.
            Label {
                text: "(" + root.library.shownCount + ")"
                color: Theme.muted
                font.pixelSize: Theme.subtitle
            }

            // As Steam's shelves: a hairline to the edge after the title.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.preferredHeight: 1
                color: Theme.stroke
            }
        }

        // A scope, so the card inside takes the keyboard when the grid makes it current.
        delegate: FocusScope {
            required property var modelData
            required property int index
            width: grid.cellWidth
            height: grid.cellHeight

            PosterCard {
                x: 4
                focus: true
                // The grid is the keyboard's stop, not each card.
                focusPolicy: Qt.ClickFocus
                width: parent.width - Theme.posterGap
                text: modelData.title
                posterUrl: modelData.posterUrl
                caption: modelData.caption
                progress: modelData.progress
                onClicked: {
                    grid.currentIndex = index
                    if (modelData.groupId)
                        root.franchiseActivated(modelData.groupId)
                    else
                        root.activated(modelData.slug)
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.section, 420)
        visible: !grid.visible
        spacing: Theme.pad

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.emptyText || "Nada que mostrar"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.emptyDetail
            color: Theme.muted
            font.pixelSize: Theme.body
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: root.emptyAction !== ""
            text: root.emptyAction
            Accessible.onPressAction: click()
            onClicked: root.emptyActed()
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
    }
}
