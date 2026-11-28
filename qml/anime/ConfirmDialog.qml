import QtQuick
import QtQuick.Controls
import AnimeHub

// A question before something that cannot be undone, with its consequence and two answers.
ModalDialog {
    id: dialog

    property string detail
    property string acceptText
    property string rejectText: "Cancelar"
    property var action: null

    function ask(question, detail, acceptText, rejectText, action) {
        dialog.title = question
        dialog.detail = detail
        dialog.acceptText = acceptText
        dialog.rejectText = rejectText
        dialog.action = action
        open()
    }

    width: 480
    onOpened: acceptButton.forceActiveFocus(InputModality.reason)
    onAccepted: if (action) action()

    // Fixed widths on both sides: Fluent's Dialog loops when its content follows it.
    Label {
        width: 432
        wrapMode: Text.Wrap
        color: Theme.ink
        text: dialog.detail
    }

    // Windows order: primary action on the left, Cancel on the right.
    footer: DialogButtonBox {
        AccentButton {
            id: acceptButton
            text: dialog.acceptText
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: dialog.rejectText
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
        }
    }
}
