import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library as a list, as Steam's: the way home and to the grid, a search, one filter, and a
// line per anime whose colour says its state: bright with something unwatched on disk, dim with
// nothing to watch, the accent while downloading. Franchises fold their anime under them.
Rectangle {
    id: root

    required property LibraryModel model
    property string selectedSlug
    property var selectedGroup: 0
    // What the pane beside it shows: the home, the grid, or an anime or a franchise.
    property bool homeShown: true
    property bool gridShown: false
    // Why the filter leaves nothing to list.
    property string emptyText

    signal homeRequested()
    signal gridRequested()
    signal activated(string slug)
    signal franchiseActivated(var groupId)
    signal removeRequested(string slug, string title)
    signal createFranchiseRequested(string slug, string title)
    signal actionFailed(string text)
    signal searchCatalog(string text)

    readonly property var filterNames: ["Todos", "Viendo", "En emisión", "Sin empezar", "Vistos",
                                        "Favoritos"]

    function activateRow(index) {
        const group = model.groupAt(index)
        if (group)
            franchiseActivated(group)
        else if (model.slugAt(index))
            activated(model.slugAt(index))
    }

    function focusSearch() {
        search.forceActiveFocus()
    }

    color: Theme.pane

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 4

            ItemDelegate {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                leftInset: 0
                rightInset: 0
                spacing: 12
                text: "Inicio"
                icon.source: Theme.source("e80f")
                highlighted: root.homeShown
                onClicked: root.homeRequested()
                Accessible.name: "Inicio de la biblioteca"
            }

            ToolButton {
                text: "Ver como cuadrícula"
                display: AbstractButton.IconOnly
                icon.source: Theme.source("f0e2")
                checkable: true
                checked: root.gridShown
                onClicked: root.gridRequested()
                ToolTip.visible: hovered
                ToolTip.text: text
            }
        }

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

        // What the list shows and how, in one place: the filter, then the two ways of showing.
        Button {
            id: filterButton
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            readonly property string label: root.model.archivedOnly
                ? "Archivo (" + root.model.archivedCount + ")"
                : root.filterNames[root.model.filter] + " (" + root.model.filterCounts[root.model.filter] + ")"
                  + (root.model.onDiskOnly ? " · en disco" : "")
            text: label
            onClicked: filterMenu.show(filterButton)
            Accessible.name: "Mostrar: " + label

            contentItem: RowLayout {
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: filterButton.label
                    elide: Text.ElideRight
                    color: Theme.ink
                    font: filterButton.font
                }

                Glyph {
                    glyph: "e70d"
                    size: 12
                    color: Theme.muted
                }
            }

            ActionMenu {
                id: filterMenu
                width: filterButton.width

                Repeater {
                    model: root.filterNames.length
                    delegate: MenuEntry {
                        required property int index
                        text: root.filterNames[index] + " (" + root.model.filterCounts[index] + ")"
                        checkable: true
                        checked: !root.model.archivedOnly && root.model.filter === index
                        onTriggered: {
                            root.model.archivedOnly = false
                            root.model.filter = index
                            root.homeRequested()
                        }
                    }
                }

                MenuEntry {
                    visible: root.model.archivedCount > 0
                    height: visible ? implicitHeight : 0
                    text: "Archivo (" + root.model.archivedCount + ")"
                    checkable: true
                    checked: root.model.archivedOnly
                    onTriggered: {
                        root.model.archivedOnly = true
                        root.homeRequested()
                    }
                }

                MenuSeparator {}

                MenuEntry {
                    text: "Solo en disco"
                    checkable: true
                    checked: root.model.onDiskOnly
                    onTriggered: root.model.onDiskOnly = !root.model.onDiskOnly
                }

                MenuEntry {
                    text: "Agrupar por franquicia"
                    checkable: true
                    checked: root.model.grouped
                    onTriggered: root.model.grouped = !root.model.grouped
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            currentIndex: root.selectedGroup ? root.model.indexOfGroup(root.selectedGroup)
                                             : root.model.indexOf(root.selectedSlug)
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: false
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: "Biblioteca"

            Keys.onReturnPressed: root.activateRow(currentIndex)
            Keys.onEnterPressed: root.activateRow(currentIndex)
            Keys.onRightPressed: {
                const group = root.model.groupAt(currentIndex)
                if (group && currentItem && !currentItem.expanded)
                    root.model.toggleGroup(group)
            }
            Keys.onLeftPressed: {
                const group = root.model.groupAt(currentIndex)
                if (group && currentItem && currentItem.expanded)
                    root.model.toggleGroup(group)
            }
            // The anime opened elsewhere (the home, a search, ← →) comes into view.
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && !activeFocus)
                    positionViewAtIndex(currentIndex, ListView.Contain)
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
                required property bool fresh
                required property bool ready
                required property bool downloading
                required property bool isGroup
                required property var groupId
                required property int depth
                required property bool expanded

                width: ListView.view.width
                height: 36
                // The selection pill keeps the margins of the fields above.
                leftInset: 12
                rightInset: 12
                topInset: 0
                bottomInset: 0
                leftPadding: 20 + depth * 20
                rightPadding: 20
                highlighted: isGroup ? groupId === root.selectedGroup : slug === root.selectedSlug
                onClicked: {
                    list.currentIndex = index
                    root.activateRow(index)
                }
                Accessible.name: title + ", " + caption
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: title + "\n" + caption

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: rowMenu.showAtPointer()
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
                        text: root.model.archivedOnly ? "Eliminar…" : "Quitar de la biblioteca…"
                        onTriggered: root.removeRequested(row.slug, row.title)
                    }
                }

                contentItem: RowLayout {
                    spacing: 8

                    // Unfolds the franchise in place; the row itself opens its page. While the list
                    // groups, every row keeps the column, so the posters line up.
                    Glyph {
                        visible: root.model.grouped
                        Layout.preferredWidth: 12
                        opacity: row.isGroup ? 1 : 0
                        glyph: row.expanded ? "e70d" : "e76c"
                        size: 10
                        color: Theme.muted

                        TapHandler {
                            enabled: row.isGroup
                            onTapped: root.model.toggleGroup(row.groupId)
                        }
                    }

                    Poster {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 28
                        source: row.posterUrl
                    }

                    Label {
                        Layout.fillWidth: true
                        text: row.title
                        // Grouped, a franchise's seasons show only what sets them apart and a
                        // title reads best cut at its end. Ungrouped, the seasons sit one above
                        // the other and differ at their end ("…Part 2"): that end stays.
                        elide: root.model.grouped ? Text.ElideRight : Text.ElideMiddle
                        font.pixelSize: Theme.body
                        color: row.downloading ? Theme.accent : row.ready ? Theme.ink : Theme.muted
                    }

                    Rectangle {
                        visible: row.fresh
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        radius: 3
                        color: Theme.accent
                    }
                }
            }

            // Nothing to list: why, and the way out.
            ColumnLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: parent.width - 2 * Theme.pad
                spacing: 12
                visible: list.count === 0 && root.model.total > 0

                readonly property bool searching: root.model.search !== ""

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    text: parent.searching ? "Ningún anime coincide con «" + root.model.search + "»"
                                           : root.emptyText
                }

                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: parent.searching ? "Buscar en el catálogo" : "Mostrar todos"
                    onClicked: {
                        if (parent.searching) {
                            root.searchCatalog(root.model.search)
                        } else {
                            root.model.onDiskOnly = false
                            root.model.filter = LibraryModel.All
                        }
                    }
                }
            }
        }
    }
}
