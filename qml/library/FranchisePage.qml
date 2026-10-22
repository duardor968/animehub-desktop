import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

Item {
    id: root
    required property LibraryModel model
    signal activated(string slug)
    signal back()
    signal removeRequested(var slugs, string title)
    signal actionFailed(string text)
    readonly property var franchise: model.franchise
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
        model: root.franchise.works || []
        currentIndex: -1
        readonly property real span: width - Theme.pad
        cellWidth: (span + Theme.posterGap) / Theme.posterColumns(span)
        cellHeight: Theme.posterCardHeight(cellWidth - Theme.posterGap) + 48
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Accessible.name: "Obras de la franquicia"
        header: Column {
            width: works.span
            spacing: Theme.pad
            bottomPadding: Theme.pad
            Item {
                x: -Theme.section
                width: root.width
                height: 260
                Image {
                    anchors.fill: parent
                    source: root.franchise.backdropUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    opacity: 0.65
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: Theme.canvas }
                    }
                }
                RowLayout {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.pad
                    ToolButton { text: "Inicio"; icon.source: "image://glyph/E72B"; onClicked: root.back() }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        id: more
                        text: "Más opciones"
                        icon.source: "image://glyph/E712"
                        display: AbstractButton.IconOnly
                        ToolTip.visible: hovered
                        ToolTip.text: text
                        onClicked: groupMenu.show(more, true)
                    }
                }
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.section
                    spacing: 8
                    Label {
                        width: parent.width
                        text: root.franchise.title || "Franquicia"
                        font.pixelSize: Theme.subtitle
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.Heading
                    }
                    Label {
                        readonly property int deliveries: (root.franchise.works || []).length
                        readonly property int files: root.franchise.files || 0
                        text: deliveries + (deliveries === 1 ? " entrega · " : " entregas · ") + files + (files === 1 ? " archivo en disco" : " archivos en disco")
                        color: Theme.muted
                    }
                }
            }
            Label { text: root.model.archivedOnly ? "Entregas en Archivo" : "Entregas"; font.pixelSize: Theme.subtitle }
        }
        delegate: Item {
            id: work
            required property var modelData
            width: works.cellWidth
            height: works.cellHeight
            PosterCard {
                id: card
                width: parent.width - Theme.posterGap
                text: work.modelData.title
                posterUrl: work.modelData.posterUrl
                caption: work.modelData.metadata
                progress: work.modelData.progress
                onClicked: root.activated(work.modelData.slug)
                Accessible.name: text + ", " + work.modelData.caption + ", " + work.modelData.diskText
                Keys.onMenuPressed: workMenu.show(card)
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: workMenu.show(card) }
            }
            Label {
                id: disk
                anchors.top: card.bottom
                anchors.topMargin: 4
                width: card.width
                text: work.modelData.diskText + " · " + work.modelData.caption
                color: Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
            }
            Label {
                anchors.top: disk.bottom
                anchors.topMargin: 3
                text: work.modelData.bytesText
                color: Theme.muted
                font.pixelSize: Theme.caption
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
