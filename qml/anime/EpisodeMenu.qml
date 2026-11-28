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
        // Its right edge on the row's button, never against the window's.
        menu.show(anchor, true)
    }

    // "Indicar dónde te quedaste…" for one episode (or the film).
    function askPosition(number, label) {
        root.number = number
        root.label = label
        continueDialog.open()
    }

    ActionMenu {
        id: menu

        // What removes things, last and apart.
        entries: [watchedEntry, throughEntry, positionEntry, fileSeparator, revealEntry,
                  removalSeparator, cancelEntry, trashEntry]

        MenuEntry {
            id: watchedEntry
            text: root.watched ? "Marcar como no visto" : "Marcar como visto"
            glyph: root.watched ? Theme.icon.undo : Theme.icon.check
            onTriggered: root.watched ? root.view.markUnwatched(root.number)
                                      : root.view.markWatched(root.number)
        }
        MenuEntry {
            id: throughEntry
            property bool wanted: !root.first
            text: "Marcar como visto hasta aquí"
            glyph: Theme.icon.check
            onTriggered: root.view.markWatchedThrough(root.number)
        }
        MenuEntry {
            id: positionEntry
            text: "Indicar dónde te quedaste…"
            glyph: Theme.icon.clock
            onTriggered: continueDialog.open()
        }
        MenuSeparator {
            id: fileSeparator
            property bool separator: true
        }
        MenuEntry {
            id: revealEntry
            property bool wanted: root.hasFile
            text: "Mostrar en la carpeta"
            glyph: Theme.icon.folder
            onTriggered: root.view.showFile(root.number)
        }
        MenuSeparator {
            id: removalSeparator
            property bool separator: true
        }
        MenuEntry {
            id: cancelEntry
            property bool wanted: root.downloadState !== ""
            text: "Cancelar la descarga"
            glyph: Theme.icon.cancel
            onTriggered: root.view.cancelDownload(root.number, false)
        }
        MenuEntry {
            id: trashEntry
            property bool wanted: root.hasFile
            text: "Mover a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: root.view.trashFile(root.number)
        }
    }

    ModalDialog {
        id: continueDialog
        width: 360
        title: "¿Dónde te quedaste?"

        readonly property string problem: root.view.clockProblem(root.number, minute.text)
        readonly property bool valid: root.view.clockValid(root.number, minute.text)

        // Windows order: primary action on the left, Cancel on the right.
        footer: DialogButtonBox {
            AccentButton {
                text: "Guardar"
                enabled: continueDialog.valid
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Accessible.onPressAction: click()
                Keys.onReturnPressed: event => { event.accepted = true; click() }
                Keys.onEnterPressed: event => { event.accepted = true; click() }
            }
        }
        onOpened: {
            minute.text = ""
            minute.forceActiveFocus(InputModality.reason)
        }
        onAccepted: root.view.continueFrom(root.number, minute.text)

        // Fixed widths: Fluent's Dialog loops when its content follows it.
        ColumnLayout {
            width: 312
            spacing: 8

            // The episode ("Episodio 3") or the film's title, as it is written.
            Label {
                Layout.fillWidth: true
                text: root.label + ", minuto"
                color: Theme.ink
                elide: Text.ElideRight
            }

            TextField {
                id: minute
                Layout.fillWidth: true
                placeholderText: "12:34"
                inputMethodHints: Qt.ImhPreferNumbers
                Accessible.name: root.label + ", minuto"
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
