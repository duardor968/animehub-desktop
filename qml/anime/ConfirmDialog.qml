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
    onAccepted: if (action) action()

    // Fluent's header, wrapping: the question names the anime and must not be cut.
    header: Label {
        text: dialog.title
        topPadding: dialog.padding
        leftPadding: dialog.padding
        rightPadding: dialog.padding
        wrapMode: Text.Wrap
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }

    // Fixed widths on both sides: Fluent's Dialog loops when its content follows it.
    Label {
        width: 432
        wrapMode: Text.Wrap
        color: Theme.ink
        text: dialog.detail
    }

    footer: DialogButtonBox {
        Button {
            text: dialog.acceptText
            highlighted: dialog.visible // DialogButtonBox clears it while it builds
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: dialog.rejectText
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
