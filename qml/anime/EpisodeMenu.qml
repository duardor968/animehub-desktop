import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The options of one episode, only those that apply to it; a single menu shared by every row.
Item {
    id: root

    required property AnimeView view

    property real number
    property string label
    property bool watched
    property bool hasFile
    property bool first
    property string downloadState

    function show(row, anchor) {
        number = row.number
        label = row.label
        watched = row.watched
        hasFile = row.hasFile
        first = row.first
        downloadState = row.downloadState
        menu.popup(anchor, 0, anchor.height)
    }

    // "Continuar desde…" for one episode (or the film).
    function askPosition(number, label) {
        root.number = number
        root.label = label
        continueDialog.open()
    }

    ActionMenu {
        id: menu

        MenuEntry {
            text: root.watched ? "Marcar como no visto" : "Marcar como visto"
            onTriggered: root.watched ? root.view.markUnwatched(root.number)
                                      : root.view.markWatched(root.number)
        }
        MenuEntry {
            visible: !root.first
            text: "Marcar como visto hasta aquí"
            onTriggered: root.view.markWatchedThrough(root.number)
        }
        MenuEntry {
            text: "Continuar desde…"
            glyph: Theme.icon.clock
            onTriggered: continueDialog.open()
        }
        MenuSeparator {
            visible: root.downloadState !== "" || root.hasFile || root.view.canSendToJDownloader
            height: visible ? implicitHeight : 0
        }
        MenuEntry {
            visible: root.downloadState !== ""
            text: "Cancelar la descarga"
            glyph: Theme.icon.cancel
            onTriggered: root.view.cancelDownload(root.number, false)
        }
        MenuEntry {
            visible: root.hasFile
            text: "Mostrar en la carpeta"
            glyph: Theme.icon.folder
            onTriggered: root.view.showFile(root.number)
        }
        MenuEntry {
            visible: root.hasFile
            text: "Mover a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: root.view.trashFile(root.number)
        }
        MenuEntry {
            visible: root.view.canSendToJDownloader && !root.hasFile
            text: "Enviar a JDownloader"
            glyph: Theme.icon.send
            onTriggered: root.view.sendToJDownloader(root.number)
        }
    }

    ModalDialog {
        id: continueDialog
        width: 360
        title: "Continuar el " + root.label.toLowerCase() + " desde"

        readonly property string problem: root.view.clockProblem(root.number, minute.text)
        readonly property bool valid: root.view.clockValid(root.number, minute.text)

        footer: DialogButtonBox {
            Button {
                text: "Guardar"
                highlighted: continueDialog.visible // DialogButtonBox clears it while it builds
                enabled: continueDialog.valid
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

        // Fixed widths: Fluent's Dialog loops when its content follows it.
        ColumnLayout {
            width: 312
            spacing: 8

            TextField {
                id: minute
                Layout.fillWidth: true
                placeholderText: "12:34"
                inputMethodHints: Qt.ImhPreferNumbers
                Accessible.name: "Minuto"
                Accessible.description: continueDialog.problem
                onAccepted: if (continueDialog.valid) continueDialog.accept()
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: continueDialog.problem
                color: Theme.danger
                font.pixelSize: Theme.caption
                wrapMode: Text.Wrap
            }
        }
    }
}
