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
    onOpened: action.forceActiveFocus()
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

    // Fluent's header, wrapping: it names the anime and must not be cut.
    header: Label {
        text: dialog.title
        topPadding: dialog.padding
        leftPadding: dialog.padding
        rightPadding: dialog.padding
        wrapMode: Text.Wrap
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }

    // Fixed widths: Fluent's Dialog loops when its content follows it.
    ColumnLayout {
        width: 432
        spacing: 16
        Keys.onReturnPressed: dialog.decide(true)
        Keys.onEnterPressed: dialog.decide(true)

        Label {
            Layout.preferredWidth: 432
            wrapMode: Text.Wrap
            color: Theme.ink
            text: dialog.downloads.offerText
        }

        CheckBox {
            id: remember
            text: "No volver a mostrar"
        }
    }

    footer: DialogButtonBox {
        Button {
            id: action
            text: dialog.jdownloader ? "Usar JDownloader" : "Activar y reintentar"
            highlighted: dialog.visible // DialogButtonBox clears it while it builds
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.decide(true)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            text: "Cerrar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Keys.onReturnPressed: dialog.reject()
            Keys.onEnterPressed: dialog.reject()
        }
    }
}
