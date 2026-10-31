import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// "Elegir un rango…": the episodes from one number to another, specials in between included. It
// already shows how many and where, so it is the only question asked.
ModalDialog {
    id: dialog

    required property AnimeView view

    readonly property int count: view.missingBetween(start.value, end.value)

    width: 420
    title: "Descargar un rango"

    onAboutToShow: {
        start.from = Math.floor(view.firstNumber)
        start.to = Math.ceil(view.lastNumber)
        end.from = start.from
        end.to = start.to
        start.value = Math.floor(view.firstMissing)
        end.value = Math.ceil(view.lastNumber)
    }
    onOpened: start.contentItem.forceActiveFocus()
    onAccepted: view.downloadRange(start.value, end.value)

    // Fixed widths: Fluent's Dialog loops when its content follows it.
    ColumnLayout {
        width: 372
        spacing: 16

        RowLayout {
            spacing: 16

            ColumnLayout {
                spacing: 6

                Label {
                    text: "Desde"
                    color: Theme.ink
                }

                SpinBox {
                    id: start
                    editable: true
                    Layout.preferredWidth: 150
                    Accessible.name: "Desde el episodio"
                }
            }

            ColumnLayout {
                spacing: 6

                Label {
                    text: "Hasta"
                    color: Theme.ink
                }

                SpinBox {
                    id: end
                    editable: true
                    Layout.preferredWidth: 150
                    Accessible.name: "Hasta el episodio"
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            text: dialog.count === 0 ? "Ya está todo en disco o en la cola"
                  : (dialog.count === 1 ? "1 episodio, en:" : dialog.count + " episodios, en:")
        }

        // The folder on a line of its own: a path breaks anywhere, never leaving "C:" alone.
        Label {
            Layout.fillWidth: true
            Layout.topMargin: -12
            visible: dialog.count > 0
            wrapMode: Text.WrapAnywhere
            color: Theme.ink
            text: dialog.view.downloadFolder
        }
    }

    footer: DialogButtonBox {
        Button {
            text: "Descargar"
            highlighted: dialog.visible // DialogButtonBox clears it while it builds
            enabled: dialog.count > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
