import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library as a list: search, a filter, and one row per anime.
Rectangle {
    id: root

    required property LibraryModel model
    property string selectedSlug
    // The home beside the list explains an empty library or filter; the list does when an anime
    // is open instead.
    property bool homeVisible: true
    // Why the filter leaves nothing to list.
    property string emptyText

    signal activated(string slug)
    signal searchCatalog(string text)

    function focusSearch() { search.forceActiveFocus() }

    color: Theme.pane

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 8

        SearchBox {
            id: search
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            placeholderText: "Buscar en la biblioteca"
            text: root.model.search
            onTextChanged: root.model.search = text
            Keys.onDownPressed: list.forceActiveFocus()
        }

        ComboBox {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            model: ["Todo", "Viendo", "Sin empezar", "Vistos", "Favoritos", "Descargados"]
            currentIndex: root.model.filter
            onActivated: index => root.model.filter = index
            Accessible.name: "Mostrar"
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            currentIndex: root.model.indexOf(root.selectedSlug)
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: false
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: "Biblioteca"

            Keys.onReturnPressed: root.activated(root.model.slugAt(currentIndex))
            Keys.onEnterPressed: root.activated(root.model.slugAt(currentIndex))
            onCurrentIndexChanged: {
                // Arrow keys move the selection like a native list.
                const slug = root.model.slugAt(currentIndex)
                if (activeFocus && slug && slug !== root.selectedSlug)
                    root.activated(slug)
            }

            delegate: ItemDelegate {
                id: row
                required property int index
                required property string slug
                required property string title
                required property url posterUrl
                required property string caption
                required property real progress
                required property bool fresh

                width: ListView.view.width
                height: 64
                highlighted: slug === root.selectedSlug
                onClicked: {
                    list.currentIndex = index
                    root.activated(slug)
                }
                Accessible.name: title + ", " + caption

                contentItem: RowLayout {
                    spacing: 12

                    Poster {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 48
                        source: row.posterUrl
                        radius: 3
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Label {
                            Layout.fillWidth: true
                            text: row.title
                            elide: Text.ElideRight
                            color: Theme.ink
                            font.pixelSize: Theme.body
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                visible: row.fresh
                                width: 6
                                height: 6
                                radius: 3
                                color: Theme.accent
                            }

                            Label {
                                Layout.fillWidth: true
                                text: row.caption
                                elide: Text.ElideRight
                                color: row.fresh ? Theme.accent : Theme.muted
                                font.pixelSize: Theme.caption
                            }
                        }

                        ProgressLine {
                            Layout.fillWidth: true
                            visible: row.progress >= 0
                            value: row.progress
                            implicitHeight: 2
                        }
                    }
                }
            }

            // Nothing to list: why, and the way out.
            ColumnLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: parent.width - 2 * Theme.pad
                spacing: 12
                visible: list.count === 0 && (searching || (!root.homeVisible && root.model.total > 0))

                readonly property bool searching: root.model.search !== ""

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    text: parent.searching ? "Nada coincide con «" + root.model.search + "»"
                                           : root.emptyText
                }

                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: parent.searching ? "Buscar en el catálogo" : "Mostrar todo"
                    onClicked: {
                        if (parent.searching)
                            root.searchCatalog(root.model.search)
                        else
                            root.model.filter = LibraryModel.All
                    }
                }
            }
        }
    }
}
