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

        readonly property int columns: Math.max(2, Math.floor((width - 16) / 176))
        cellWidth: Math.floor((width - 16) / columns)
        cellHeight: Math.round((cellWidth - 20) * 1.42) + 78

        header: Column {
            width: grid.width - 16
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
                width: parent.width - 20
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
