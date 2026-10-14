import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library as a list: search, a filter, and one row per anime.
Rectangle {
    id: root

    required property LibraryModel model
    property string selectedSlug

    signal activated(string slug)

    color: Theme.pane

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 8

        TextField {
            id: search
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            placeholderText: "Buscar en la biblioteca"
            text: root.model.search
            onTextEdited: root.model.search = text
            Keys.onDownPressed: list.forceActiveFocus()
            Accessible.name: "Buscar en la biblioteca"
        }

        ComboBox {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            model: ["Todo", "Pendientes", "Favoritos", "Vistos", "Descargados"]
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

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.muted
                text: root.model.total === 0 ? "Tu biblioteca está vacía"
                                             : "Nada coincide con la búsqueda"
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        enabled: root.visible
        onActivated: search.forceActiveFocus()
    }
}
