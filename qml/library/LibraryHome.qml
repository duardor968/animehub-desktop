import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library seen as posters: what is being watched, what has news, and everything.
Item {
    id: root

    required property LibraryModel library

    signal activated(string slug)
    signal browseCatalog()

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: 32
        anchors.rightMargin: 16
        visible: root.library.total > 0
        clip: true
        model: root.library.allShelf
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: "Toda la biblioteca"

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

        header: Column {
            width: grid.span
            topPadding: 28
            spacing: 32
            bottomPadding: 16

            Shelf {
                width: parent.width
                title: "Continuar viendo"
                model: root.library.continueShelf
                onActivated: slug => root.activated(slug)
            }

            Shelf {
                width: parent.width
                title: "Episodios nuevos"
                model: root.library.freshShelf
                onActivated: slug => root.activated(slug)
            }

            Label {
                text: "Toda la biblioteca"
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
                progress: model.progress
                fresh: model.fresh
                onClicked: root.activated(model.slug)
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: root.library.total === 0
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Tu biblioteca está vacía"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Explorar el catálogo"
            highlighted: true
            onClicked: root.browseCatalog()
        }
    }
}
