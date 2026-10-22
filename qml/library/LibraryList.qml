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
    signal franchiseActivated(var groupId)
    signal archiveActivated()
    signal removeRequested(string slug, string title)
    signal createFranchiseRequested(string slug, string title)
    signal actionFailed(string text)
    signal reviewCandidatesRequested()
    property var selectedGroup: 0
    function activateRow(index) {
        const group = root.model.groupAt(index)
        if (group) root.franchiseActivated(group)
        else {
            const slug = root.model.slugAt(index)
            if (slug) root.activated(slug)
        }
    }
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
            objectName: "libraryViewMode"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            model: ["Colección", "Todos los animes", "Solo franquicias"]
            currentIndex: root.model.viewMode
            onActivated: index => root.model.viewMode = index
            Accessible.name: "Vista de biblioteca"
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
            currentIndex: root.selectedGroup ? root.model.indexOfGroup(root.selectedGroup) : root.model.indexOf(root.selectedSlug)
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: false
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: "Biblioteca"

            Keys.onReturnPressed: root.activateRow(currentIndex)
            Keys.onEnterPressed: root.activateRow(currentIndex)
            Keys.onRightPressed: {
                const group = root.model.groupAt(currentIndex)
                if (group && currentItem && !currentItem.expanded) root.model.toggleGroup(group)
            }
            Keys.onLeftPressed: {
                const group = root.model.groupAt(currentIndex)
                if (group && currentItem && currentItem.expanded) root.model.toggleGroup(group)
            }
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
                required property bool isGroup
                required property var groupId
                required property int depth
                required property bool expanded

                width: ListView.view.width
                height: 64
                highlighted: isGroup ? groupId === root.selectedGroup : slug === root.selectedSlug
                onClicked: {
                    list.currentIndex = index
                    root.activateRow(index)
                }
                Accessible.name: title + ", " + caption

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: rowMenu.show(row)
                }
                Keys.onMenuPressed: rowMenu.show(row)
                ActionMenu {
                    id: rowMenu
                    entries: [openRow, createRow, restoreRow, removeRow]
                    MenuEntry {
                        id: openRow
                        text: row.isGroup ? "Abrir franquicia" : "Abrir anime"
                        onTriggered: root.activateRow(row.index)
                    }
                    MenuEntry {
                        id: createRow
                        property bool wanted: !row.isGroup
                        text: "Crear franquicia…"
                        onTriggered: root.createFranchiseRequested(row.slug, row.title)
                    }
                    MenuEntry {
                        id: restoreRow
                        property bool wanted: !row.isGroup && root.model.archivedOnly
                        text: "Restaurar a la biblioteca"
                        onTriggered: {
                            if (!root.model.archive([row.slug], false))
                                root.actionFailed(root.model.error)
                        }
                    }
                    MenuEntry {
                        id: removeRow
                        property bool wanted: !row.isGroup
                        text: root.model.archivedOnly ? "Eliminar registro personal…" : "Quitar de la biblioteca…"
                        onTriggered: root.removeRequested(row.slug, row.title)
                    }
                }
                contentItem: RowLayout {
                    spacing: 12

                    ToolButton {
                        visible: row.isGroup && root.model.viewMode === LibraryModel.Collection
                        Layout.preferredWidth: 32
                        leftPadding: 0
                        rightPadding: 0
                        text: row.expanded ? "Contraer" : "Expandir"
                        icon.source: "image://glyph/" + (row.expanded ? "E70D" : "E76C")
                        icon.width: 16
                        icon.height: 16
                        display: AbstractButton.IconOnly
                        onClicked: root.model.toggleGroup(row.groupId)
                        Accessible.name: text + " " + row.title
                        ToolTip.visible: hovered
                        ToolTip.text: text
                    }
                    Item { visible: row.depth > 0; Layout.preferredWidth: 32 }
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
        ToolButton {
            objectName: "libraryReviewRelations"
            Layout.fillWidth: true
            visible: root.model.pendingCandidates.length > 0
            text: "Revisar relaciones (" + root.model.pendingCandidates.length + ")"
            icon.source: "image://glyph/E946"
            onClicked: root.reviewCandidatesRequested()
            Accessible.name: text
        }
        ToolButton {
            objectName: "libraryArchiveLink"
            Layout.fillWidth: true
            text: root.model.archivedOnly ? "Volver a la biblioteca" : "Archivo · " + root.model.archivedCount
            icon.source: "image://glyph/E7B8"
            onClicked: root.archiveActivated()
            Accessible.name: text
        }
    }
}
