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
    readonly property bool several: slugs.length > 1
    signal completed()
    width: 520
    title: archived ? (several ? "Eliminar registros personales" : "Eliminar registro personal")
                    : "Quitar de la biblioteca"
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
            text: dialog.several ? dialog.recordTitle + " · " + dialog.slugs.length + " obras"
                                 : dialog.recordTitle
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: {
                const removal = "Al eliminar " + (dialog.several ? "los registros personales" : "el registro personal")
                    + ", se borran el visto, el progreso y el favorito. Los vídeos siguen en el disco, pero dejan de figurar como descargados."
                if (dialog.archived)
                    return removal
                return (dialog.several ? "Archivar las guarda en Archivo tal como están" : "Archivar lo guarda en Archivo tal como está")
                    + ", avisos incluidos. " + removal
            }
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
            text: "Eliminar"
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
