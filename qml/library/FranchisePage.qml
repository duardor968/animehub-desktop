import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A franchise laid out as an anime's page: the same header and bar, the main button going on
// with the work under way, then its works as posters, one stop for the keyboard.
Item {
    id: root
    required property LibraryModel model
    // The places above the page, for the breadcrumb over the title: [{text, go}].
    property var steps: []
    readonly property Item actionBar: bar
    signal activated(string slug)
    // Esc goes back as the window's ← does.
    signal back()
    signal removeRequested(var slugs, string title)
    signal actionFailed(string text)
    readonly property var franchise: model.franchise
    readonly property var workList: franchise.works || []
    readonly property int slugCount: franchise.slugs ? franchise.slugs.length : 0
    property var lastFranchiseId: 0
    property bool positioningOnOpen: false
    function openAddDialog() {
        choices.model = root.model.memberChoices(root.franchise.id)
        addDialog.open()
    }
    function positionBeginningWhenReady() {
        if (!positioningOnOpen || width <= 0 || height <= 0)
            return
        // StackLayout can expose the page before assigning its size. Lay out the
        // header at that size before GridView computes its scroll extents.
        if (works.headerItem)
            works.headerItem.forceLayout()
        works.forceLayout()
        works.positionViewAtBeginning()
        positioningOnOpen = false
    }
    function showBeginning() {
        positioningOnOpen = true
        Qt.callLater(positionBeginningWhenReady)
    }
    function focusTitle(reason) {
        if (works.headerItem)
            works.headerItem.focusTitle(reason !== undefined ? reason : InputModality.reason)
    }
    Keys.onEscapePressed: root.back()
    // Plays the episode a work goes on with, or downloads it when it is not on disk.
    function continueWith(slug) {
        const win = ApplicationWindow.window
        if (win && win.app)
            win.app.continueWatching(slug)
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
        bottomMargin: 8
        model: root.workList
        activeFocusOnTab: true
        // A card reached by keyboard brings itself into view; the header stays on opening.
        highlightFollowsCurrentItem: false
        readonly property real span: width - Theme.pad
        cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
        // The card and its second line, with the work's state.
        cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap) + 40
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: "Animes de la franquicia"
        header: Column {
            readonly property real barY: barSlot.y
            function focusTitle(reason) { franchiseHeader.focusTitle(reason) }
            readonly property Item titleItem: franchiseHeader.titleItem
            width: works.span
            AnimeHeader {
                id: franchiseHeader
                steps: root.steps
                bar: root.actionBar
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
                height: bar.unpinnedHeight
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
        footer: Item {
            visible: root.workList.length === 0
            width: works.span
            height: visible ? emptyFranchise.implicitHeight + 48 : 0

            ColumnLayout {
                id: emptyFranchise
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: Math.min(parent.width, 420)
                spacing: Theme.pad

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: root.model.archivedOnly
                          ? "No hay entregas de esta franquicia en Archivo"
                          : "Esta franquicia no tiene animes todavía"
                    color: Theme.ink
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.model.archivedOnly
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: "Añade animes de tu biblioteca para agruparlos aquí."
                    color: Theme.muted
                    font.pixelSize: Theme.body
                }

                AccentButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !root.model.archivedOnly
                    text: "Añadir anime…"
                    glyphCode: Theme.icon.add
                    onClicked: root.openAddDialog()
                }
            }
        }
        // A scope, so the card inside takes the keyboard when the grid makes it current.
        delegate: FocusScope {
            id: work
            required property var modelData
            required property int index
            // "24 episodios en disco · 5,8 GB": the size is only in the tooltip.
            readonly property string disk: modelData.bytesText !== ""
                                           ? modelData.diskText + " · " + modelData.bytesText : ""
            width: works.cellWidth
            height: works.cellHeight
            PosterCard {
                id: card
                focus: true
                // The grid is the keyboard's stop, not each card.
                focusPolicy: Qt.ClickFocus
                width: parent.width - Theme.posterGap
                text: work.modelData.shortTitle
                posterUrl: work.modelData.posterUrl
                caption: work.modelData.facts
                // Where the user is with the work.
                detail: work.modelData.state
                progress: work.modelData.progress
                fresh: work.modelData.fresh
                quickAction: work.modelData.action
                readonly property bool overActionDisc: {
                    if (!card.showAction || !cardPointer.hovered)
                        return false
                    const dx = cardPointer.point.position.x - card.width / 2
                    const dy = cardPointer.point.position.y - Math.round(card.width * 1.42) / 2
                    return dx * dx + dy * dy <= 23 * 23
                }
                // The whole title and what it takes on disk, yielding to the centered action disc.
                ToolTip.visible: (hovered && !overActionDisc)
                                 || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: work.modelData.title + (work.disk !== "" ? "\n" + work.disk : "")
                onClicked: {
                    works.currentIndex = work.index
                    root.activated(work.modelData.slug)
                }
                onAct: root.continueWith(work.modelData.slug)
                Accessible.name: work.modelData.title + ", " + work.modelData.facts + ", "
                                 + work.modelData.state + (work.disk !== "" ? ", " + work.disk : "")
                Keys.onMenuPressed: workMenu.openFor(work.modelData, card)
                HoverHandler { id: cardPointer }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: workMenu.openFor(work.modelData, null) }
            }
        }
    }
    ActionMenu {
        id: workMenu
        property var workData: ({})
        entries: [open, detach, workMenuSeparator, restore, remove]
        function openFor(data, anchor) {
            workData = data || ({})
            if (anchor)
                show(anchor)
            else
                showAtPointer()
        }
        MenuEntry {
            id: open
            glyph: Theme.icon.openExternal
            text: "Abrir anime"
            onTriggered: root.activated(workMenu.workData.slug)
        }
        MenuEntry {
            id: detach
            glyph: Theme.icon.remove
            text: "Quitar de esta franquicia"
            onTriggered: {
                if (!root.model.setFranchiseMember(root.franchise.id, workMenu.workData.slug, false))
                    root.actionFailed(root.model.error)
            }
        }
        MenuSeparator {
            id: workMenuSeparator
            property bool separator: true
        }
        MenuEntry {
            id: restore
            property bool wanted: root.model.archivedOnly
            glyph: Theme.icon.undo
            text: "Restaurar a la biblioteca"
            onTriggered: {
                if (!root.model.archive([workMenu.workData.slug], false))
                    root.actionFailed(root.model.error)
            }
        }
        MenuEntry {
            id: remove
            glyph: root.model.archivedOnly ? Theme.icon.trash : Theme.icon.remove
            text: root.model.archivedOnly ? "Eliminar registro personal…" : "Quitar de la biblioteca…"
            onTriggered: root.removeRequested([workMenu.workData.slug], workMenu.workData.title)
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
        titleItem: works.headerItem ? works.headerItem.titleItem : null
        posterUrl: root.franchise.posterUrl || ""
        figures: root.franchise.figures || []
        // Goes on with the work under way ("Ver ep. 6", "Descargar ep. 6"), named beside it; with
        // everything seen, there is none.
        AccentButton {
            readonly property string step: root.franchise.nextAction || ""
            visible: (root.franchise.nextSlug || "") !== ""
            // Its download on its way: nothing to press until it is on disk.
            enabled: step !== "busy"
            emphasized: enabled
            implicitHeight: 36
            height: 36
            // As wide as an anime's main button, in every state.
            implicitWidth: Math.max(200, implicitContentWidth + leftPadding + rightPadding)
            text: root.franchise.nextText || ""
            glyphCode: step === "play" ? Theme.icon.play
                       : step === "retry" ? Theme.icon.retry
                       : Theme.icon.download
            iconSize: 14
            onClicked: root.continueWith(root.franchise.nextSlug)
        }
        trailing: [
            ToolButton {
                id: more
                text: "Más opciones"
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                Accessible.onPressAction: click()
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                onClicked: groupMenu.show(more, true)
            }
        ]
    }
    ActionMenu {
        id: groupMenu
        entries: [addWork, groupMenuSeparator, restoreGroup, removeGroup]
        MenuEntry {
            id: addWork
            glyph: Theme.icon.add
            text: "Añadir anime…"
            onTriggered: root.openAddDialog()
        }
        MenuSeparator {
            id: groupMenuSeparator
            property bool separator: true
        }
        MenuEntry {
            id: restoreGroup
            property bool wanted: root.model.archivedOnly && root.slugCount > 0
            glyph: Theme.icon.undo
            text: "Restaurar a la biblioteca"
            onTriggered: {
                if (!root.model.archive(root.franchise.slugs, false))
                    root.actionFailed(root.model.error)
            }
        }
        MenuEntry {
            id: removeGroup
            property bool wanted: root.slugCount > 0
            glyph: root.model.archivedOnly ? Theme.icon.trash : Theme.icon.remove
            text: root.model.archivedOnly
                  ? (root.slugCount === 1 ? "Eliminar registro personal…" : "Eliminar registros personales…")
                  : "Quitar de la biblioteca…"
            onTriggered: root.removeRequested(root.franchise.slugs, root.franchise.title)
        }
    }
    ModalDialog {
        id: addDialog
        property string failure: ""
        width: 480
        title: "Añadir anime a la franquicia"
        onAboutToShow: failure = ""
        onOpened: (choices.count > 0 ? choices : addCancelButton).forceActiveFocus(InputModality.reason)
        ColumnLayout {
            width: 432
            spacing: 12
            ComboBox {
                id: choices
                Layout.fillWidth: true
                textRole: "title"
                valueRole: "slug"
                Accessible.name: "Anime de la biblioteca"
            }
            Label {
                Layout.fillWidth: true
                visible: choices.count === 0
                text: "Todos los animes de la biblioteca están en esta franquicia."
                color: Theme.muted
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                visible: addDialog.failure !== ""
                spacing: 6

                Glyph {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    glyph: Theme.icon.warning
                    size: 13
                    color: Theme.danger
                }

                Label {
                    Layout.fillWidth: true
                    text: addDialog.failure
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: Theme.caption
                }
            }
        }
        footer: DialogButtonBox {
            AccentButton {
                text: "Añadir anime"
                implicitHeight: 32
                enabled: choices.currentIndex >= 0
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    if (root.model.setFranchiseMember(root.franchise.id, choices.currentValue, true))
                        addDialog.close()
                    else
                        addDialog.failure = root.model.error
                }
            }
            Button {
                id: addCancelButton
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Keys.onReturnPressed: addDialog.reject()
                Keys.onEnterPressed: addDialog.reject()
            }
        }
    }
}
