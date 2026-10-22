import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

ModalDialog {
    id: dialog
    required property RemoteView remote
    property string revokeId: ""
    title: revokeId ? "¿Desvincular este navegador?" : "Dispositivos vinculados"
    width: Math.min(520, parent.width - 48)
    height: Math.min(implicitHeight, parent.height - 48)
    onOpened: { revokeId = ""; remote.refresh() }
    ColumnLayout {
        width: parent.width
        spacing: 16
        Label {
            visible: !dialog.revokeId && dialog.remote.registered
            text: dialog.remote.deviceName
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: !dialog.revokeId && !dialog.remote.registered
            Layout.fillWidth: true
            Label { text: "Nombre de este PC" }
            TextField {
                id: deviceName
                text: "Mi PC"
                Accessible.name: "Nombre de este PC"
                Layout.fillWidth: true
                maximumLength: 80
            }
            Button {
                text: "Activar vinculación"
                enabled: !dialog.remote.busy && deviceName.text.trim().length > 0
                onClicked: dialog.remote.registerDevice(deviceName.text)
                Accessible.name: text
            }
        }
        ColumnLayout {
            visible: !dialog.revokeId && dialog.remote.registered
            Layout.fillWidth: true
            Repeater {
                model: dialog.remote.links
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        text: modelData.name + " · " + new Date(modelData.createdAt).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Button {
                        text: "Desvincular…"
                        enabled: !dialog.remote.busy
                        onClicked: dialog.revokeId = modelData.id
                        Accessible.name: "Desvincular navegador"
                    }
                }
            }
            Label { text: "Vincular un dispositivo"; font.weight: Font.DemiBold }
            Label { text: "Introduce el código en la web de AnimeHub."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label {
                visible: dialog.remote.code.length > 0
                text: dialog.remote.code
                font.pixelSize: 30
                Accessible.name: "Código de vinculación: " + text
            }
            Label {
                visible: dialog.remote.expiresAt.length > 0
                text: "Válido hasta " + new Date(dialog.remote.expiresAt).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
            }
            Button {
                text: dialog.remote.code ? "Generar otro código" : "Generar código"
                enabled: !dialog.remote.busy
                onClicked: dialog.remote.generatePairingCode()
                Accessible.name: text
            }
        }
        Label {
            visible: !!dialog.revokeId
            text: "Las solicitudes pendientes de este PC se cancelarán. Las ya recibidas continuarán en Desktop."
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: dialog.remote.error.length > 0
            text: dialog.remote.error
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    footer: Loader {
        sourceComponent: dialog.revokeId ? revokeButtons : closeButtons
    }
    Component {
        id: closeButtons
        DialogButtonBox {
            Button {
                text: "Cerrar"
                onClicked: dialog.reject()
                Accessible.name: text
            }
        }
    }
    Component {
        id: revokeButtons
        DialogButtonBox {
            Button {
                text: "Cancelar"
                onClicked: dialog.revokeId = ""
                Accessible.name: text
            }
            Button {
                text: "Desvincular"
                enabled: !dialog.remote.busy
                onClicked: { dialog.remote.revokeLink(dialog.revokeId); dialog.revokeId = "" }
                Accessible.name: text
            }
        }
    }
}
