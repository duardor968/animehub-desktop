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
    width: Math.min(580, parent.width - 48)
    title: "¿Seguir en la bandeja?"
    onAboutToShow: remember.checked = false
    // The main button has the keyboard, as in a Windows dialog: Enter takes it, Tab moves on.
    onOpened: stay.forceActiveFocus()

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 16
        // Enter on the check box takes the main button too.
        Keys.onReturnPressed: dialog.choose(AppSettings.Tray)
        Keys.onEnterPressed: dialog.choose(AppSettings.Tray)

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.ink
            text: "Las descargas y los avisos siguen en segundo plano."
        }

        CheckBox {
            id: remember
            text: "No volver a preguntar"
        }
    }

    // Windows order: the main action, the other one, then Cancelar.
    footer: DialogButtonBox {
        Button {
            id: stay
            text: "Seguir en la bandeja"
            // The button box clears `highlighted` while it lays its buttons out; a binding that
            // changes on opening sets it again.
            highlighted: dialog.visible
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.choose(AppSettings.Tray)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            text: "Salir"
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: dialog.choose(AppSettings.Quit)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Keys.onReturnPressed: dialog.reject()
            Keys.onEnterPressed: dialog.reject()
        }
    }
}
