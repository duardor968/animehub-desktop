import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A franchise laid out as an anime's page: the same header and bar, the main button going on
// with the work under way, then its works as posters.
Item {
    id: root
    required property LibraryModel model
    signal activated(string slug)
    // Unused: the window has its own back and forward.
    signal back()
    signal removeRequested(var slugs, string title)
    signal actionFailed(string text)
    readonly property var franchise: model.franchise
    readonly property var workList: franchise.works || []
    property var lastFranchiseId: 0
    property bool positioningOnOpen: false
    function positionBeginningWhenReady() {
        if (!positioningOnOpen || width <= 0 || height <= 0)
            return
        // StackLayout can expose the page before assigning its size. Lay out the
        // header at that size before GridView computes its scroll extents.
        works.headerItem.forceLayout()
        works.forceLayout()
        works.positionViewAtBeginning()
        positioningOnOpen = false
    }
    function showBeginning() {
        positioningOnOpen = true
        Qt.callLater(positionBeginningWhenReady)
    }
    // Plays the episode a work goes on with, or downloads it when it is not on disk.
    function continueWith(slug) {
        ApplicationWindow.window.app.continueWatching(slug)
    }
    onWidthChanged: if (positioningOnOpen) Qt.callLater(positionBeginningWhenReady)
    onHeightChanged: if (positioningOnOpen) Qt.callLater(positionBeginningWhenReady)
    onVisibleChanged: if (visible) showBeginning()
    onFranchiseChanged: {
        if (franchise.id !== lastFranchiseId) {
            lastFranchiseId = franchise.id
            showBeginning()
        }
    }
    clip: true
    GridView {
        id: works
        objectName: "franchiseWorks"
        anchors.fill: parent
        anchors.leftMargin: Theme.section
        anchors.rightMargin: Theme.pad
        model: root.workList
        currentIndex: -1
        readonly property real span: width - Theme.pad
        cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
        // The card and the line under it with the work's state.
        cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap) + 40
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: "Obras de la franquicia"
        header: Column {
            readonly property real barY: barSlot.y
            width: works.span
            AnimeHeader {
                x: -Theme.section
                width: root.width
                title: root.franchise.title || "Franquicia"
                facts: root.franchise.facts || ""
                posterUrl: root.franchise.posterUrl || ""
                backdropUrl: root.franchise.backdropUrl || ""
            }
            // Where the bar sits until it reaches the top.
            Item {
                id: barSlot
                width: parent.width
                height: bar.height
            }
            RowLayout {
                width: parent.width
                height: 72
                spacing: 8
                Label {
                    text: root.model.archivedOnly ? "Entregas en Archivo" : "Entregas"
                    color: Theme.ink
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                }
                Label {
                    text: "(" + root.workList.length + ")"
                    color: Theme.muted
                    font.pixelSize: Theme.subtitle
                }
                Item { Layout.fillWidth: true }
            }
        }
        delegate: Item {
            id: work
            required property var modelData
            // "24 episodios en disco · 5,8 GB": the size is only in the tooltip.
            readonly property string disk: modelData.bytesText !== ""
                                           ? modelData.diskText + " · " + modelData.bytesText : ""
            width: works.cellWidth
            height: works.cellHeight
            PosterCard {
                id: card
                width: parent.width - Theme.posterGap
                text: work.modelData.shortTitle
                posterUrl: work.modelData.posterUrl
                caption: work.modelData.facts
                progress: work.modelData.progress
                fresh: work.modelData.fresh
                quickAction: work.modelData.action
                // The whole title and what it takes on disk, whether or not the title is cut.
                ToolTip.visible: hovered
                ToolTip.text: work.modelData.title + (work.disk !== "" ? "\n" + work.disk : "")
                onClicked: root.activated(work.modelData.slug)
                onAct: root.continueWith(work.modelData.slug)
                Accessible.name: work.modelData.title + ", " + work.modelData.facts + ", "
                                 + work.modelData.state + (work.disk !== "" ? ", " + work.disk : "")
                Keys.onMenuPressed: workMenu.show(card)
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: workMenu.show(card) }
            }
            // The card's second caption: where the user is with the work.
            Label {
                anchors.top: card.bottom
                anchors.topMargin: -2
                width: card.width
                text: work.modelData.state
                color: Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
            }
            ActionMenu {
                id: workMenu
                entries: [open, detach, restore, remove]
                MenuEntry { id: open; text: "Abrir anime"; onTriggered: root.activated(work.modelData.slug) }
                MenuEntry {
                    id: detach
                    text: "Quitar de esta franquicia"
                    onTriggered: {
                        if (!root.model.setFranchiseMember(root.franchise.id, work.modelData.slug, false))
                            root.actionFailed(root.model.error)
                    }
                }
                MenuEntry {
                    id: restore
                    property bool wanted: root.model.archivedOnly
                    text: "Restaurar a la biblioteca"
                    onTriggered: {
                        if (!root.model.archive([work.modelData.slug], false))
                            root.actionFailed(root.model.error)
                    }
                }
                MenuEntry {
                    id: remove
                    text: root.model.archivedOnly ? "Eliminar registro personal…" : "Quitar de la biblioteca…"
                    onTriggered: root.removeRequested([work.modelData.slug], work.modelData.title)
                }
            }
        }
    }
    ActionBar {
        id: bar
        // Where the header leaves it, until it reaches the top.
        readonly property real slotY: {
            const header = works.headerItem
            return header ? header.y + header.barY - works.contentY : 0
        }
        anchors.left: parent.left
        anchors.right: parent.right
        y: Math.max(0, slotY)
        pinned: slotY < 0
        visible: works.headerItem !== null
        title: root.franchise.title || ""
        posterUrl: root.franchise.posterUrl || ""
        figures: root.franchise.figures || []
        // Goes on with the work under way ("Ver ep. 6", "Descargar ep. 6"), named beside it; with
        // everything seen, there is none.
        Button {
            readonly property string step: root.franchise.nextAction || ""
            visible: (root.franchise.nextSlug || "") !== ""
            // Its download on its way: nothing to press until it is on disk.
            enabled: step !== "busy"
            // Fluent keeps a disabled accent button blue: only a button that can be pressed is.
            highlighted: enabled
            // As wide as an anime's main button, in every state.
            implicitWidth: Math.max(200, implicitContentWidth + leftPadding + rightPadding)
            text: root.franchise.nextText || ""
            icon.source: Theme.source(step === "play" ? Theme.icon.play
                                      : step === "retry" ? Theme.icon.retry
                                      : Theme.icon.download)
            onClicked: root.continueWith(root.franchise.nextSlug)
        }
        trailing: [
            ToolButton {
                id: more
                text: "Más opciones"
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: groupMenu.show(more, true)
            }
        ]
    }
    ActionMenu {
        id: groupMenu
        entries: [addWork, restoreGroup, removeGroup]
        MenuEntry {
            id: addWork
            text: "Añadir obra…"
            onTriggered: {
                choices.model = root.model.memberChoices(root.franchise.id)
                addDialog.open()
            }
        }
        MenuEntry {
            id: restoreGroup
            property bool wanted: root.model.archivedOnly
            text: "Restaurar a la biblioteca"
            onTriggered: {
                if (!root.model.archive(root.franchise.slugs, false))
                    root.actionFailed(root.model.error)
            }
        }
        MenuEntry {
            id: removeGroup
            text: root.model.archivedOnly ? "Eliminar registros personales…" : "Quitar de la biblioteca…"
            onTriggered: root.removeRequested(root.franchise.slugs, root.franchise.title)
        }
    }
    ModalDialog {
        id: addDialog
        property string failure: ""
        width: 480
        title: "Añadir obra a la franquicia"
        onAboutToShow: { failure = ""; choices.forceActiveFocus() }
        ColumnLayout {
            width: 432
            ComboBox {
                id: choices
                Layout.fillWidth: true
                textRole: "title"
                valueRole: "slug"
                Accessible.name: "Obra de la biblioteca"
            }
            Label { Layout.fillWidth: true; visible: choices.count === 0; text: "Todas las obras de la biblioteca están en esta franquicia."; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; visible: text !== ""; text: addDialog.failure; wrapMode: Text.Wrap; color: Theme.muted }
        }
        footer: DialogButtonBox {
            Button {
                text: "Añadir obra"
                enabled: choices.currentIndex >= 0
                highlighted: true
                onClicked: {
                    if (root.model.setFranchiseMember(root.franchise.id, choices.currentValue, true))
                        addDialog.close()
                    else
                        addDialog.failure = root.model.error
                }
            }
            Button { text: "Cancelar"; onClicked: addDialog.reject() }
        }
    }
}
