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
    focus: true
    onAboutToShow: remember.checked = false

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 16
        // Enter takes the main button, as in a Windows dialog.
        focus: true
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
            text: "Seguir en la bandeja"
            // The button box clears `highlighted` while it lays its buttons out; a binding that
            // changes on opening sets it again.
            highlighted: dialog.visible
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.choose(AppSettings.Tray)
        }
        Button {
            text: "Salir"
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: dialog.choose(AppSettings.Quit)
        }
        Button {
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
