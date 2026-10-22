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

    signal browseCatalog()
    signal searchCatalog(string text)
    signal actionFailed(string text)

    // Why the list filter leaves nothing, by filter.
    readonly property string emptyText: ["", "No estás viendo ningún anime",
                                         "Ningún anime sin empezar", "Ningún anime visto",
                                         "Ningún favorito",
                                         "Ningún anime descargado"][app.library.filter]

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
            SplitView.preferredWidth: page.app.settings.value("library/listWidth", 300)
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 480
            model: page.app.library
            onActionFailed: text => page.actionFailed(text)
            onReviewCandidatesRequested: candidateReview.show()
            selectedSlug: page.selected
            homeVisible: !page.selected && !page.selectedGroup
            selectedGroup: page.selectedGroup
            onFranchiseActivated: id => {
                page.selected = ""
                page.selectedGroup = id
                page.app.library.openFranchise(id)
            }
            onArchiveActivated: {
                page.select("")
                page.app.library.archivedOnly = !page.app.library.archivedOnly
            }
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
    FranchiseCandidatesDialog {
        id: candidateReview
        model: page.app.library
        onResolved: (id, archived) => {
            page.app.library.archivedOnly = archived
            page.selected = ""
            page.selectedGroup = id
            page.app.library.openFranchise(id)
        }
    }
    ModalDialog {
        id: createGroup
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
            TextField { id: groupTitle; Layout.fillWidth: true; placeholderText: "Nombre de la franquicia"; Accessible.name: "Nombre de la franquicia"; onAccepted: createGroup.submit() }
            Label { Layout.fillWidth: true; visible: text !== ""; text: createGroup.failure; wrapMode: Text.Wrap; color: Theme.muted }
        }
        footer: DialogButtonBox {
            Button {
                text: "Crear franquicia"
                enabled: groupTitle.text.trim().length > 0
                highlighted: true
                onClicked: createGroup.submit()
            }
            Button { text: "Cancelar"; onClicked: createGroup.reject() }
        }
    }
}
