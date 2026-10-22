import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

ModalDialog {
    id: dialog
    objectName: "franchiseReviewDialog"
    required property LibraryModel model
    property var candidate: ({})
    property string failure: ""
    readonly property bool creating: destination.currentValue === -1
    signal resolved(var groupId, bool archived)
    title: "Revisar relaciones"
    width: Math.min(580, parent.width - 48)
    height: Math.min(560, parent.height - 48)
    onOpened: destination.forceActiveFocus()

    function next() {
        const pending = model.pendingCandidates
        if (!pending.length) { close(); return false }
        candidate = pending[0]
        failure = ""
        destination.model = [{id: 0, title: "Seleccionar franquicia…"}].concat(model.franchiseChoices).concat([{id: -1, title: "Crear franquicia…"}])
        destination.currentIndex = 0
        newTitle.text = ""
        bodyScroll.contentItem.contentY = 0
        destination.forceActiveFocus()
        return true
    }
    function show() { if (next()) open() }
    function submit() {
        if (!confirm.enabled) return
        const groupId = destination.currentValue
        const success = creating ? model.createCandidateFranchise(candidate.slug, newTitle.text)
                                 : model.resolveCandidate(candidate.slug, groupId)
        if (!success) { failure = model.error; return }
        const resolvedId = creating ? model.franchise.id : groupId
        const group = model.franchiseChoices.find(item => item.id === resolvedId)
        resolved(resolvedId, group ? group.archivedOnly : false)
        next()
    }
    contentItem: ScrollView {
        id: bodyScroll
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: bodyScroll.availableWidth
            spacing: Theme.pad
            Label {
                Layout.fillWidth: true
                text: dialog.model.pendingCandidates.length + (dialog.model.pendingCandidates.length === 1 ? " relación por revisar" : " relaciones por revisar")
                color: Theme.muted
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.pad
                Poster { Layout.preferredWidth: 54; Layout.preferredHeight: 76; source: dialog.candidate.posterUrl || "" }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: dialog.candidate.title || ""; font.bold: true; wrapMode: Text.Wrap }
                    Label { Layout.fillWidth: true; text: dialog.candidate.reasonText || ""; color: Theme.muted; wrapMode: Text.Wrap }
                }
            }
            Label { text: "Franquicia" }
            ComboBox {
                id: destination
                objectName: "candidateDestination"
                Layout.fillWidth: true
                textRole: "title"
                valueRole: "id"
                Accessible.name: "Franquicia"
            }
            Label { visible: dialog.creating; text: "Nombre de la franquicia" }
            TextField {
                id: newTitle
                objectName: "candidateNewTitle"
                Layout.fillWidth: true
                visible: dialog.creating
                Accessible.name: "Nombre de la franquicia"
                onAccepted: dialog.submit()
            }
            Label {
                Layout.fillWidth: true
                visible: dialog.creating
                text: "Obras en la nueva franquicia: " + (dialog.candidate.standaloneWorks || []).map(work => work.title).concat([dialog.candidate.title || ""]).join(", ") + "."
                wrapMode: Text.Wrap
                color: Theme.muted
            }
            Label {
                Layout.fillWidth: true
                text: dialog.candidate.inLibrary ? "Se conservarán el seguimiento y el historial de la obra. No se descargarán archivos."
                                                : "Se añadirá a la biblioteca con seguimiento. No se descargarán archivos."
                wrapMode: Text.Wrap
                color: Theme.muted
            }
            Label { Layout.fillWidth: true; text: "Ignorar descarta la propuesta. No elimina obras ni historial."; wrapMode: Text.Wrap; color: Theme.muted }
            Label { Layout.fillWidth: true; visible: text !== ""; text: dialog.failure; wrapMode: Text.Wrap; color: Theme.muted }
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "candidateIgnore"
            text: "Ignorar propuesta"
            onClicked: {
                if (dialog.model.dismissCandidate(dialog.candidate.slug)) dialog.next()
                else dialog.failure = dialog.model.error
            }
        }
        Button { objectName: "candidateClose"; text: "Cerrar"; onClicked: dialog.reject() }
        Button {
            id: confirm
            objectName: "candidateConfirm"
            text: dialog.creating ? "Crear franquicia" : "Añadir a franquicia"
            highlighted: true
            enabled: destination.currentIndex > 0 && (!dialog.creating || newTitle.text.trim().length > 0)
            onClicked: dialog.submit()
        }
    }
}
