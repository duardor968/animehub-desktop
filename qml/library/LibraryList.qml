import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library as a list, as Steam's: the way home and to the grid, a search, one filter, and a
// line per anime, bright with something unwatched on disk and dim with nothing to watch.
// Franchises fold their anime under them. For the keyboard the list is one stop: the arrows
// open what they reach, franchise or anime, without taking the keyboard from the list.
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
    // `quiet`: reached with the arrows, the keyboard stays in the list.
    signal activated(string slug, bool quiet)
    signal franchiseActivated(var groupId, bool quiet)
    signal removeRequested(string slug, string title)
    signal createFranchiseRequested(string slug, string title)
    signal actionFailed(string text)
    signal searchCatalog(string text)

    // By LibraryModel.Filter; the menu lists them in `filterOrder`.
    readonly property var filterNames: ["Todos", "Viendo", "En emisión", "Sin empezar", "Vistos",
                                        "Favoritos", "Para ver"]
    readonly property var filterOrder: [LibraryModel.All, LibraryModel.ToWatch, LibraryModel.Watching,
                                        LibraryModel.Airing, LibraryModel.NotStarted,
                                        LibraryModel.Watched, LibraryModel.Favorites]

    function activateRow(index, quiet) {
        const group = model.groupAt(index)
        if (group)
            franchiseActivated(group, quiet)
        else if (model.slugAt(index))
            activated(model.slugAt(index), quiet)
    }

    // The row of what the pane beside shows, set here rather than bound: the arrow keys move it
    // too, and a binding would be lost at the first.
    function syncCurrent() {
        list.currentIndex = selectedGroup ? model.indexOfGroup(selectedGroup) : model.indexOf(selectedSlug)
    }
    onSelectedSlugChanged: syncCurrent()
    onSelectedGroupChanged: syncCurrent()
    Connections {
        target: root.model
        function onModelReset() { root.syncCurrent() }
        function onLayoutChanged() { root.syncCurrent() }
        function onRowsInserted() { root.syncCurrent() }
        function onRowsRemoved() { root.syncCurrent() }
    }

    function focusSearch() {
        search.forceActiveFocus(InputModality.reason)
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
                // A stop for the keyboard, as everything that can be pressed.
                focusPolicy: Qt.StrongFocus
                onClicked: root.homeRequested()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Accessible.name: "Inicio de la biblioteca"
            }

            ToolButton {
                text: "Cuadrícula"
                display: AbstractButton.TextBesideIcon
                icon.source: Theme.source("f0e2")
                checkable: true
                checked: root.gridShown
                onClicked: root.gridRequested()
                Accessible.name: "Ver la biblioteca como cuadrícula"
                ToolTip.visible: hovered
                ToolTip.text: "Ver la biblioteca como cuadrícula"
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
            Keys.onDownPressed: list.forceActiveFocus(InputModality.reason)
        }

        // What the list shows and how, in one place: the filter, then the smart expand/collapse all toggle.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 6

            Button {
                id: filterButton
                Layout.fillWidth: true
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
                        model: root.filterOrder
                        delegate: MenuEntry {
                            required property int modelData
                            text: root.filterNames[modelData] + " (" + root.model.filterCounts[modelData] + ")"
                            checkable: true
                            checked: !root.model.archivedOnly && root.model.filter === modelData
                            onTriggered: {
                                root.model.archivedOnly = false
                                root.model.filter = modelData
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

            ToolButton {
                id: toggleGroupsButton
                objectName: "toggleGroupsButton"
                visible: root.model.grouped && root.model.hasGroups
                display: AbstractButton.IconOnly
                icon.source: Theme.source(root.model.allExpanded ? Theme.icon.collapseAll
                                                                 : Theme.icon.expandAll)
                text: root.model.allExpanded ? "Contraer todas las franquicias"
                                             : "Expandir todas las franquicias"
                Accessible.name: text
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: root.model.allExpanded ? root.model.collapseAll() : root.model.expandAll()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            // The rows keep the margins of the fields above; the keyboard's frame, around a row,
            // stays inside the pane.
            leftMargin: 12
            rightMargin: 12
            topMargin: 4
            bottomMargin: 4
            keyNavigationEnabled: true
            activeFocusOnTab: true
            highlightFollowsCurrentItem: false
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: "Biblioteca"
            Component.onCompleted: root.syncCurrent()

            // Enter takes the keyboard to the page it opens.
            Keys.onReturnPressed: root.activateRow(currentIndex, false)
            Keys.onEnterPressed: root.activateRow(currentIndex, false)
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
            // The row with the keyboard, or the one opened elsewhere (the home, a search, ← →),
            // comes into view; the arrows open what they reach, as a native list selects it.
            onCurrentIndexChanged: {
                if (currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)
                if (!activeFocus || currentIndex < 0)
                    return
                const group = root.model.groupAt(currentIndex)
                const slug = root.model.slugAt(currentIndex)
                if (group ? group !== root.selectedGroup : slug && slug !== root.selectedSlug)
                    root.activateRow(currentIndex, true)
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
                required property bool isGroup
                required property var groupId
                required property int depth
                required property bool expanded

                width: ListView.view.width - 24
                height: 36
                leftInset: 0
                rightInset: 0
                topInset: 0
                bottomInset: 0
                // Override FluentWinUI3's inherited topPadding/bottomPadding: 8 so the 28 px poster
                // stays centered inside the 36 px hover pill without protruding below it.
                topPadding: 0
                bottomPadding: 0
                // Keep >= 8 px left offset so Fluent's active left bar (x=4..7 px) stays clear;
                // top-level grouped rows reserve a 28×36 px chevron column so all posters align at x=38.
                leftPadding: root.model.grouped ? (depth === 0 ? 8 : 38 + depth * 20) : 14
                rightPadding: 8
                highlighted: isGroup ? groupId === root.selectedGroup : slug === root.selectedSlug
                // The list is the keyboard's stop; a click focuses the list on this row.
                focusPolicy: Qt.NoFocus
                onClicked: {
                    list.forceActiveFocus(Qt.MouseFocusReason)
                    root.activateRow(index, false)
                }
                Accessible.name: title + ", " + caption
                ToolTip.visible: hovered && !chevronBtn.hovered && titleLabel.truncated
                ToolTip.delay: 600
                ToolTip.text: title + "\n" + caption

                // Fluent frames the row reached with Tab; the arrows move the keyboard without
                // that, so the same frame is drawn here.
                FocusFrame {
                    visible: row.activeFocus && !row.visualFocus && InputModality.keyboard
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: rowMenu.openFor(row.index, row.slug, row.title, row.isGroup, null)
                }
                Keys.onMenuPressed: rowMenu.openFor(row.index, row.slug, row.title, row.isGroup, row)

                contentItem: RowLayout {
                    spacing: 8

                    // Unfolds the franchise in place; 28×36 px click-isolated button never opens
                    // FranchisePage. Non-group top-level rows keep the column so posters align.
                    Item {
                        visible: root.model.grouped && row.depth === 0
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 36
                        Layout.rightMargin: -6

                        AbstractButton {
                            id: chevronBtn
                            visible: row.isGroup
                            anchors.fill: parent
                            padding: 0
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            text: row.expanded ? "Contraer " + row.title : "Expandir " + row.title
                            Accessible.name: text
                            onClicked: root.model.toggleGroup(row.groupId)

                            background: Rectangle {
                                anchors.fill: parent
                                anchors.leftMargin: 3
                                anchors.rightMargin: 3
                                anchors.topMargin: 6
                                anchors.bottomMargin: 6
                                radius: 4
                                color: chevronBtn.down ? Qt.rgba(1, 1, 1, 0.12)
                                       : chevronBtn.hovered ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                            }

                            contentItem: Item {
                                Glyph {
                                    anchors.centerIn: parent
                                    glyph: row.expanded ? "e70d" : "e76c"
                                    size: 10
                                    color: chevronBtn.hovered ? Theme.ink : Theme.muted
                                }
                            }
                        }
                    }

                    Poster {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 28
                        Layout.alignment: Qt.AlignVCenter
                        source: row.posterUrl
                    }

                    Label {
                        id: titleLabel
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: row.title
                        // Cut in the middle in either view: titles that share a beginning differ
                        // at their end ("…Part 2", "…2nd Season"), and both ends stay.
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.body
                        font.weight: row.highlighted ? Font.DemiBold : Font.Normal
                        color: row.highlighted ? "#ffffff" : (row.ready ? Theme.ink : Theme.muted)
                    }

                    Rectangle {
                        visible: row.fresh
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        Layout.alignment: Qt.AlignVCenter
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
                    // A search looks through the whole library, whatever the filter.
                    text: parent.searching ? "Nada en la biblioteca coincide con «" + root.model.search + "»"
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

    // One shared row context menu for the entire library list (avoids N*5 QML objects per reload).
    ActionMenu {
        id: rowMenu
        objectName: "libraryRowMenu"

        property int rowIndex: -1
        property string rowSlug: ""
        property string rowTitle: ""
        property bool rowIsGroup: false

        entries: [openRow, createRow, groupsSeparator, toggleAllGroupsRow, removeSeparator, restoreRow, removeRow]

        function openFor(index, slug, title, isGroup, anchor) {
            rowIndex = index
            rowSlug = slug
            rowTitle = title
            rowIsGroup = isGroup
            if (anchor)
                show(anchor)
            else
                showAtPointer()
        }

        function openForSlug(slug) {
            const index = root.model.indexOf(slug)
            if (index < 0)
                return false
            list.positionViewAtIndex(index, ListView.Contain)
            const item = list.itemAtIndex(index)
            openFor(index, slug, item ? item.title : slug, false, item || list)
            return true
        }

        MenuEntry {
            id: openRow
            text: rowMenu.rowIsGroup ? "Abrir franquicia" : "Abrir anime"
            onTriggered: root.activateRow(rowMenu.rowIndex, false)
        }

        MenuEntry {
            id: createRow
            property bool wanted: !rowMenu.rowIsGroup
            text: "Crear franquicia…"
            onTriggered: root.createFranchiseRequested(rowMenu.rowSlug, rowMenu.rowTitle)
        }

        MenuSeparator {
            id: groupsSeparator
            property bool separator: true
        }

        MenuEntry {
            id: toggleAllGroupsRow
            property bool wanted: root.model.grouped && root.model.hasGroups
            glyph: root.model.allExpanded ? Theme.icon.collapseAll : Theme.icon.expandAll
            text: root.model.allExpanded ? "Contraer todas las franquicias"
                                         : "Expandir todas las franquicias"
            onTriggered: root.model.allExpanded ? root.model.collapseAll() : root.model.expandAll()
        }

        MenuSeparator {
            id: removeSeparator
            property bool separator: true
        }

        MenuEntry {
            id: restoreRow
            objectName: "restoreArchivedEntry"
            property bool wanted: !rowMenu.rowIsGroup && root.model.archivedOnly
            text: "Restaurar a la biblioteca"
            onTriggered: {
                if (!root.model.archive([rowMenu.rowSlug], false))
                    root.actionFailed(root.model.error)
            }
        }

        MenuEntry {
            id: removeRow
            property bool wanted: !rowMenu.rowIsGroup
            text: root.model.archivedOnly ? "Eliminar…" : "Quitar de la biblioteca…"
            onTriggered: root.removeRequested(rowMenu.rowSlug, rowMenu.rowTitle)
        }
    }
}
