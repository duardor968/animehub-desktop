import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

    header: Label {
        text: dialog.title
        topPadding: dialog.padding
        leftPadding: dialog.padding
        rightPadding: dialog.padding
        wrapMode: Text.Wrap
        color: Theme.ink
        font.pixelSize: 20
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
    }

    // Fixed width: Fluent's Dialog loops when its content follows availableWidth.
    Label {
        width: 412
        wrapMode: Text.Wrap
        color: Theme.muted
        font.pixelSize: Theme.body - 1
        lineHeight: 1.4
        text: dialog.activeCount === 1
              ? "Tienes 1 descarga en curso. Se pausará al reiniciar y continuará sola al "
                + "volver a abrir AnimeHub. Si hay un vídeo abierto en mpv, no se detendrá."
              : "Tienes " + dialog.activeCount + " descargas en curso. Se pausarán al "
                + "reiniciar y continuarán solas al volver a abrir AnimeHub. Si hay un "
                + "vídeo abierto en mpv, no se detendrá."
    }

    footer: Item {
        implicitHeight: footerRow.implicitHeight + 28

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            color: dialog.palette.window
            bottomLeftRadius: 7
            bottomRightRadius: 7

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.stroke
            }
        }

        RowLayout {
            id: footerRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                id: laterButton
                text: "Más tarde"
                Accessible.name: "Más tarde"
                onClicked: dialog.reject()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    dialog.reject()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    dialog.reject()
                }
            }

            AccentButton {
                id: confirmButton
                objectName: "updateRestartConfirmButton"
                text: "Actualizar y reiniciar"
                glyphCode: Theme.icon.update
                fontPixelSize: Theme.body - 1
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                Accessible.onPressAction: click()
                onClicked: {
                    dialog.accept()
                    dialog.updater.applyAndRestart()
                }
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    confirmButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    confirmButton.click()
                }
            }
        }
    }
}
