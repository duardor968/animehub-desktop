import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Asked when the window closes until a choice is saved: keep running in the tray, quit, or
// leave the window open (Esc).
ModalDialog {
    id: dialog

    required property AppController app

    function choose(action) {
        close()
        app.closeTo(action, remember.checked)
    }

    // Three buttons of equal width that fit "Seguir en la bandeja".
    width: Math.min(580, parent ? parent.width - 48 : 580)
    title: "¿Seguir en la bandeja?"
    onAboutToShow: remember.checked = false
    // The main button has the keyboard, as in a Windows dialog: Enter takes it, Tab moves on.
    onOpened: stay.forceActiveFocus(InputModality.reason)

    ColumnLayout {
        width: dialog.availableWidth
        spacing: Theme.pad
        // Enter on the check box takes the main button too.
        Keys.onReturnPressed: event => {
            event.accepted = true
            dialog.choose(AppSettings.Tray)
        }
        Keys.onEnterPressed: event => {
            event.accepted = true
            dialog.choose(AppSettings.Tray)
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.ink
            font.pixelSize: Theme.body
            text: "Las descargas y los avisos siguen en segundo plano."
        }

        CheckBox {
            id: remember
            text: "No volver a preguntar"
        }
    }

    // Windows order: the main action, the other one, then Cancelar.
    footer: DialogButtonBox {
        AccentButton {
            id: stay
            text: "Seguir en la bandeja"
            fontPixelSize: Theme.small
            implicitHeight: 32
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.choose(AppSettings.Tray)
        }
        Button {
            id: quitButton
            text: "Salir"
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            Accessible.onPressAction: quitButton.click()
            onClicked: dialog.choose(AppSettings.Quit)
            Keys.onReturnPressed: event => {
                event.accepted = true
                quitButton.click()
            }
            Keys.onEnterPressed: event => {
                event.accepted = true
                quitButton.click()
            }
        }
        Button {
            id: cancelButton
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Accessible.onPressAction: cancelButton.click()
            Keys.onReturnPressed: event => {
                event.accepted = true
                dialog.reject()
            }
            Keys.onEnterPressed: event => {
                event.accepted = true
                dialog.reject()
            }
        }
    }
}
