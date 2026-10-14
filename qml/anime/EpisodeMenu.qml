import QtQuick
import QtQuick.Controls
import AnimeHub

// The options of one episode; a single menu shared by every row of the page.
Item {
    id: root

    required property AnimeView view

    property real number
    property string label
    property bool watched
    property bool hasFile
    property string downloadState

    function show(row, anchor) {
        number = row.number
        label = row.label
        watched = row.watched
        hasFile = row.hasFile
        downloadState = row.downloadState
        menu.popup(anchor, 0, anchor.height)
    }

    ActionMenu {
        id: menu

        MenuEntry {
            text: root.watched ? "Marcar como no visto" : "Marcar como visto"
            onTriggered: root.watched ? root.view.markUnwatched(root.number)
                                      : root.view.markWatched(root.number)
        }
        MenuEntry {
            text: "Marcar como visto hasta aquí"
            onTriggered: root.view.markWatchedThrough(root.number)
        }
        MenuEntry {
            text: "Continuar desde…"
            glyph: Theme.icon.clock
            onTriggered: continueDialog.open()
        }
        MenuSeparator {}
        MenuEntry {
            text: "Cancelar la descarga"
            visible: root.downloadState !== ""
            glyph: Theme.icon.cancel
            onTriggered: root.view.cancelDownload(root.number)
        }
        MenuEntry {
            text: "Mostrar en la carpeta"
            visible: root.hasFile
            glyph: Theme.icon.folder
            onTriggered: root.view.showFile(root.number)
        }
        MenuEntry {
            text: "Mover el archivo a la papelera"
            visible: root.hasFile
            glyph: Theme.icon.trash
            onTriggered: root.view.trashFile(root.number)
        }
        MenuEntry {
            text: "Enviar a JDownloader"
            visible: root.view.canSendToJDownloader
            glyph: Theme.icon.send
            onTriggered: root.view.sendToJDownloader(root.number)
        }
    }

    ModalDialog {
        id: continueDialog
        title: "Continuar el " + root.label.toLowerCase() + " desde"

        footer: DialogButtonBox {
            Button {
                text: "Guardar"
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onOpened: {
            minute.text = ""
            minute.forceActiveFocus()
        }
        onAccepted: root.view.continueFrom(root.number, minute.text)

        TextField {
            id: minute
            width: 220
            placeholderText: "12:34"
            inputMethodHints: Qt.ImhPreferNumbers
            Accessible.name: "Minuto"
            onAccepted: continueDialog.accept()
        }
    }
}
