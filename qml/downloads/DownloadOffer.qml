import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// After an episode no enabled server has: JDownloader for the servers only it downloads, or
// turning on the servers that have it. Its own button opens the next step; closing it (Esc
// too) leaves the episode as it is.
ModalDialog {
    id: dialog
    objectName: "downloadOffer"

    required property DownloadsView downloads
    readonly property bool jdownloader: downloads.offer === "jdownloader"
    // Asked for JDownloader's setup.
    signal setupRequested()

    function decide(accepted) {
        const never = remember.checked
        // Read before the offer goes.
        const setup = accepted && jdownloader
        if (visible)
            close()
        if (accepted)
            downloads.acceptOffer(never)
        else
            downloads.dismissOffer(never)
        if (setup)
            setupRequested()
    }

    width: 480
    title: downloads.offerTitle
    closePolicy: Popup.CloseOnEscape
    onAboutToShow: remember.checked = false
    onOpened: acceptButton.forceActiveFocus(InputModality.reason)
    onAccepted: decide(true)
    onRejected: decide(false)

    Connections {
        target: dialog.downloads
        function onOfferChanged() {
            // Also while the last one is still closing.
            if (dialog.downloads.offer !== "")
                dialog.open()
            else if (dialog.downloads.offer === "" && dialog.visible)
                dialog.close()
        }
    }

    // Fixed widths: Fluent's Dialog loops when its content follows it.
    ColumnLayout {
        width: 432
        spacing: 16
        Keys.onReturnPressed: event => {
            event.accepted = true
            dialog.accept()
        }
        Keys.onEnterPressed: event => {
            event.accepted = true
            dialog.accept()
        }

        Label {
            Layout.preferredWidth: 432
            wrapMode: Text.Wrap
            color: Theme.ink
            font.pixelSize: Theme.body
            text: dialog.downloads.offerText
        }

        CheckBox {
            id: remember
            text: "No volver a mostrar"
        }
    }

    footer: DialogButtonBox {
        AccentButton {
            id: acceptButton
            text: dialog.jdownloader ? "Usar JDownloader" : "Activar y reintentar"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            id: closeButton
            text: "Cerrar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Accessible.onPressAction: dialog.reject()
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
