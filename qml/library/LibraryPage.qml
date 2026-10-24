import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Steam-style library: the list on the left; on the right, the home by task when nothing is
// selected, the list as a grid, or the selected anime or franchise.
Item {
    id: page

    required property AppController app
    property string selected
    property var selectedGroup: 0
    // The right pane shows the list as posters.
    property bool showGrid: false

    signal browseCatalog()
    signal searchCatalog(string text)
    signal actionFailed(string text)

    // Why the list filter leaves nothing, by filter.
    readonly property string emptyText: app.library.archivedOnly ? "El Archivo está vacío" : ({
        [LibraryModel.Watching]: "No estás viendo ningún anime",
        [LibraryModel.Airing]: "Ningún anime en emisión",
        [LibraryModel.NotStarted]: "Ningún anime sin empezar",
        [LibraryModel.Watched]: "Ningún anime visto",
        [LibraryModel.Favorites]: "Ningún favorito"
    })[app.library.filter] ?? (app.library.onDiskOnly ? "Ningún anime en disco" : "")

    // The list comes back as it was left; the model corrects an unknown value. INI settings
    // come back as text.
    Component.onCompleted: {
        app.library.filter = Number(app.settings.value("library/filter", 0))
        app.library.onDiskOnly = String(app.settings.value("library/onDiskOnly", false)) === "true"
        app.library.grouped = String(app.settings.value("library/grouped", true)) === "true"
    }
    Connections {
        target: page.app.library
        function onFilterChanged() {
            page.app.settings.setValue("library/filter", page.app.library.filter)
            page.app.settings.setValue("library/onDiskOnly", page.app.library.onDiskOnly)
            page.app.settings.setValue("library/grouped", page.app.library.grouped)
        }
    }

    function select(slug) {
        selectedGroup = 0
        showGrid = false
        selected = slug
        if (slug)
            app.openLibraryAnime(slug)
    }

    function openFranchise(id) {
        selected = ""
        showGrid = false
        selectedGroup = id
        app.library.openFranchise(id)
    }

    function focusSearch() { list.focusSearch() }

    // Where it is, for the window's back and forward: an anime, a franchise, the grid or home.
    function location() {
        return { slug: selected, group: selectedGroup, grid: showGrid }
    }
    function restore(place) {
        if (place.group)
            openFranchise(place.group)
        else
            select(place.slug)
        showGrid = place.grid
    }

    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Horizontal

        // A hairline between the panes, with no gap beside it; the pointer grabs it 4 px away.
        handle: Rectangle {
            id: hairline
            implicitWidth: 1
            color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.stroke
            containmentMask: Item {
                x: (hairline.width - width) / 2
                width: 9
                height: split.height
            }
        }

        LibraryList {
            id: list
            SplitView.preferredWidth: page.app.settings.value("library/listWidth", 300)
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 480
            model: page.app.library
            selectedSlug: page.selected
            selectedGroup: page.selectedGroup
            homeShown: !page.selected && !page.selectedGroup && !page.showGrid
            gridShown: page.showGrid
            emptyText: page.emptyText
            onHomeRequested: page.select("")
            onGridRequested: {
                const open = !page.showGrid
                page.select("")
                page.showGrid = open
            }
            onActivated: slug => page.select(slug)
            onFranchiseActivated: id => page.openFranchise(id)
            onActionFailed: text => page.actionFailed(text)
            onRemoveRequested: (slug, title) => removal.show([slug], title, page.app.library.archivedOnly)
            onCreateFranchiseRequested: (slug, title) => {
                createGroup.slug = slug
                groupTitle.text = title
                createGroup.open()
            }
            onSearchCatalog: text => page.searchCatalog(text)
            onWidthChanged: if (width > 0) page.app.settings.setValue("library/listWidth", Math.round(width))
        }

        StackLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 480
            // Not the widest child's implicit width (an unwrapped synopsis): the split decides.
            implicitWidth: 480
            currentIndex: page.selectedGroup ? 2 : page.selected ? 1 : page.showGrid ? 3 : 0

            LibraryHome {
                app: page.app
                onActivated: slug => page.select(slug)
                onBrowseCatalog: page.browseCatalog()
            }

            AnimePage {
                view: page.app.libraryAnime
                onBack: page.select("")
            }

            FranchisePage {
                model: page.app.library
                onActionFailed: text => page.actionFailed(text)
                onActivated: slug => page.select(slug)
                onBack: page.select("")
                onRemoveRequested: (slugs, title) => removal.show(slugs, title, page.app.library.archivedOnly)
            }

            LibraryGrid {
                library: page.app.library
                emptyText: page.emptyText
                onActivated: slug => page.select(slug)
                onFranchiseActivated: id => page.openFranchise(id)
            }
        }
    }

    LibraryRemovalDialog {
        id: removal
        model: page.app.library
        onCompleted: page.select("")
    }
    ModalDialog {
        id: createGroup
        objectName: "createFranchiseDialog"
        property string slug
        property string failure: ""
        width: 480
        title: "Crear franquicia"
        function submit() {
            if (!groupTitle.text.trim().length) return
            if (page.app.library.createFranchise(groupTitle.text, slug)) {
                page.selected = ""
                page.selectedGroup = page.app.library.franchise.id
                close()
            } else {
                failure = page.app.library.error
            }
        }
        // The anime's title is a suggestion: typing replaces it.
        onAboutToShow: { failure = ""; groupTitle.forceActiveFocus(); groupTitle.selectAll() }
        ColumnLayout {
            width: 432
            TextField { id: groupTitle; objectName: "createFranchiseTitle"; Layout.fillWidth: true; placeholderText: "Nombre de la franquicia"; Accessible.name: "Nombre de la franquicia"; onAccepted: createGroup.submit() }
            Label { Layout.fillWidth: true; visible: text !== ""; text: createGroup.failure; wrapMode: Text.Wrap; color: Theme.muted }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "createFranchiseConfirm"
                text: "Crear franquicia"
                enabled: groupTitle.text.trim().length > 0
                highlighted: createGroup.visible // DialogButtonBox clears it while it builds
                onClicked: createGroup.submit()
            }
            Button { text: "Cancelar"; onClicked: createGroup.reject() }
        }
    }
}
