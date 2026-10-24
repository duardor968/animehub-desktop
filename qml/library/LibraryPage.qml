import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Steam-style library: the list on the left; on the right, the home with posters when nothing
// is selected, or the selected anime.
Item {
    id: page

    required property AppController app
    property string selected
    property var selectedGroup: 0
    // PROTOTYPE: "A" keeps what to watch and to download on the home, "B" in the list's filter.
    property string variant: "A"

    signal browseCatalog()
    signal searchCatalog(string text)
    signal actionFailed(string text)

    // Why the list filter leaves nothing, by filter.
    readonly property string emptyText: ({
        [LibraryModel.Franchises]: "Ninguna franquicia",
        [LibraryModel.ToWatch]: "Nada que ver en disco",
        [LibraryModel.ToDownload]: "Nada por descargar",
        [LibraryModel.Airing]: "Ningún anime en emisión",
        [LibraryModel.Watching]: "No estás viendo ningún anime",
        [LibraryModel.NotStarted]: "Ningún anime sin empezar",
        [LibraryModel.Watched]: "Ningún anime visto",
        [LibraryModel.Favorites]: "Ningún favorito",
        [LibraryModel.OnDisk]: "Ningún anime en disco"
    })[app.library.filter] ?? ""

    // The filter it was left with comes back; the model corrects an unknown one.
    Component.onCompleted: app.library.filter = Number(app.settings.value("library/filter", 0))
    Connections {
        target: page.app.library
        function onFilterChanged() { page.app.settings.setValue("library/filter", page.app.library.filter) }
    }
    // PROTOTYPE: A has no task views in its filter.
    onVariantChanged: if (variant === "A" && (app.library.filter === LibraryModel.ToWatch
                                              || app.library.filter === LibraryModel.ToDownload))
                          app.library.filter = LibraryModel.All

    function select(slug) {
        selectedGroup = 0
        selected = slug
        if (slug)
            app.openLibraryAnime(slug)
    }

    function focusSearch() { list.focusSearch() }

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
            variant: page.variant
            SplitView.preferredWidth: page.app.settings.value("library/listWidth", 300)
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 480
            model: page.app.library
            onActionFailed: text => page.actionFailed(text)
            selectedSlug: page.selected
            homeVisible: !page.selected && !page.selectedGroup
            selectedGroup: page.selectedGroup
            onFranchiseActivated: id => {
                page.selected = ""
                page.selectedGroup = id
                page.app.library.openFranchise(id)
            }
            onFilterChosen: page.select("")
            onRemoveRequested: (slug, title) => removal.show([slug], title, page.app.library.archivedOnly)
            onCreateFranchiseRequested: (slug, title) => {
                createGroup.slug = slug
                groupTitle.text = title
                createGroup.open()
            }
            emptyText: page.emptyText
            onActivated: slug => page.select(slug)
            onSearchCatalog: text => page.searchCatalog(text)
            onWidthChanged: if (width > 0) page.app.settings.setValue("library/listWidth", Math.round(width))
        }

        StackLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 480
            // Not the widest child's implicit width (an unwrapped synopsis): the split decides.
            implicitWidth: 480
            currentIndex: page.selectedGroup ? 2 : page.selected ? 1 : 0

            LibraryHome {
                app: page.app
                variant: page.variant
                emptyText: page.emptyText
                onActivated: slug => page.select(slug)
                onFranchiseActivated: id => {
                    page.selected = ""
                    page.selectedGroup = id
                    page.app.library.openFranchise(id)
                }
                onBrowseCatalog: page.browseCatalog()
            }

            AnimePage {
                view: page.app.libraryAnime
                backText: "Inicio"
                onBack: page.select("")
            }
            FranchisePage {
                model: page.app.library
                onActionFailed: text => page.actionFailed(text)
                onActivated: slug => page.select(slug)
                onBack: page.select("")
                onRemoveRequested: (slugs, title) => removal.show(slugs, title, page.app.library.archivedOnly)
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
        onAboutToShow: { failure = ""; groupTitle.forceActiveFocus() }
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
                highlighted: true
                onClicked: createGroup.submit()
            }
            Button { text: "Cancelar"; onClicked: createGroup.reject() }
        }
    }
}
