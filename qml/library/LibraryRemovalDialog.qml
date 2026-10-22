import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

ModalDialog {
    id: dialog
    required property LibraryModel model
    property var slugs: []
    property string recordTitle
    property bool archived: false
    signal completed()
    width: 520
    title: archived ? "Eliminar registro personal" : "Quitar de la biblioteca"
    function show(records, name, isArchived) {
        slugs = records
        recordTitle = name
        archived = isArchived
        open()
    }
    ColumnLayout {
        width: 472
        spacing: 16
        Label {
            Layout.fillWidth: true
            text: dialog.recordTitle
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: dialog.archived
                ? "Se olvidarán el visto, el progreso y el seguimiento. Los vídeos permanecerán en el disco."
                : "Archivar conserva el visto, el progreso y el seguimiento. Podrás restaurarlo desde Archivo. Eliminar el registro personal olvida esos datos. Los vídeos permanecerán en el disco."
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.model.error !== ""
            text: dialog.model.error
            color: Theme.muted
            wrapMode: Text.Wrap
        }
    }
    footer: DialogButtonBox {
        Button {
            visible: !dialog.archived
            text: "Archivar"
            highlighted: dialog.visible
            onClicked: {
                if (dialog.model.archive(dialog.slugs, true)) {
                    dialog.close()
                    dialog.completed()
                }
            }
        }
        Button {
            text: "Eliminar registro personal"
            onClicked: {
                if (dialog.model.removePersonal(dialog.slugs)) {
                    dialog.close()
                    dialog.completed()
                }
            }
        }
        Button { text: "Cancelar"; onClicked: dialog.reject() }
    }
}
