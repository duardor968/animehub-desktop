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
    onOpened: (dialog.archived ? cancelButton : archiveButton).forceActiveFocus(InputModality.reason)
    function show(records, name, isArchived) {
        slugs = records
        recordTitle = name
        archived = isArchived
        open()
    }
    ColumnLayout {
        width: 472
        spacing: 14
        Label {
            Layout.fillWidth: true
            text: dialog.several ? dialog.recordTitle + " · " + dialog.slugs.length + " animes"
                                 : dialog.recordTitle
            color: Theme.ink
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            color: Theme.ink
            text: {
                const removal = "Al eliminar " + (dialog.several ? "los registros personales" : "el registro personal")
                    + ", se borran el visto, el progreso y el favorito de forma permanente (esta acción no se puede deshacer). "
                    + "Los vídeos siguen en el disco, pero dejan de figurar como descargados."
                if (dialog.archived)
                    return removal
                return (dialog.several ? "Archivar los guarda en Archivo tal como están" : "Archivar lo guarda en Archivo tal como está")
                    + ", avisos incluidos. " + removal
            }
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.model ? dialog.model.error !== "" : false
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
                text: dialog.model ? dialog.model.error : ""
                color: Theme.danger
                font.pixelSize: Theme.caption
                wrapMode: Text.Wrap
            }
        }
    }
    footer: DialogButtonBox {
        AccentButton {
            id: archiveButton
            visible: !dialog.archived
            text: "Archivar"
            implicitHeight: 32
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (dialog.model.archive(dialog.slugs, true)) {
                    dialog.close()
                    dialog.completed()
                }
            }
        }
        Button {
            id: removeButton
            text: "Eliminar"
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (dialog.model.removePersonal(dialog.slugs)) {
                    dialog.close()
                    dialog.completed()
                }
            }
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            id: cancelButton
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Keys.onReturnPressed: dialog.reject()
            Keys.onEnterPressed: dialog.reject()
        }
    }
}
