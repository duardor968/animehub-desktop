import QtQuick
import QtQuick.Controls
import AnimeHub

// Confirmation dialog shown ONLY when applying a ready update while active downloads are running.
ModalDialog {
    id: dialog
    objectName: "updateRestartDialog"

    required property AppController app
    readonly property UpdateView updater: app.updater
    readonly property int activeCount: updater.activeDownloadsCount

    width: 460
    title: "Actualizar a " + updater.targetVersion
    closePolicy: Popup.CloseOnEscape
    onOpened: confirmButton.forceActiveFocus(InputModality.reason)
    onAccepted: dialog.updater.applyAndRestart()

    // Fixed width: Fluent's Dialog loops when its content follows availableWidth.
    Label {
        width: 412
        wrapMode: Text.Wrap
        color: Theme.muted
        font.pixelSize: Theme.small
        lineHeight: 1.4
        text: dialog.activeCount === 1
              ? "Tienes 1 descarga en curso. Se pausará al reiniciar y continuará sola al "
                + "volver a abrir AnimeHub. Si hay un vídeo abierto en mpv, no se detendrá."
              : "Tienes " + dialog.activeCount + " descargas en curso. Se pausarán al "
                + "reiniciar y continuarán solas al volver a abrir AnimeHub. Si hay un "
                + "vídeo abierto en mpv, no se detendrá."
    }

    // Windows order: primary action on the left, secondary on the right.
    footer: DialogButtonBox {
        AccentButton {
            id: confirmButton
            objectName: "updateRestartConfirmButton"
            text: "Actualizar y reiniciar"
            glyphCode: Theme.icon.update
            fontPixelSize: Theme.small
            implicitHeight: 32
            leftPadding: 13
            rightPadding: 13
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }

        Button {
            id: laterButton
            text: "Más tarde"
            Accessible.name: "Más tarde"
            Accessible.onPressAction: laterButton.click()
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Keys.onReturnPressed: event => {
                event.accepted = true
                laterButton.click()
            }
            Keys.onEnterPressed: event => {
                event.accepted = true
                laterButton.click()
            }
        }
    }
}
