import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Ajustes › Dispositivos vinculados: this PC joins the AnimeHub website, which can then send it
// downloads from any browser linked to it. Unlinking one is confirmed in its own line.
ColumnLayout {
    id: page

    required property RemoteView remote
    property int controlWidth: 320
    // The browser whose unlinking waits for confirmation.
    property string revokeId: ""

    // Each time the page comes into view.
    function refresh() {
        revokeId = ""
        remote.refresh()
    }

    spacing: 0

    Label {
        Layout.fillWidth: true
        Layout.bottomMargin: 8
        visible: !page.remote.registered
        text: "Vincula este PC con la web de AnimeHub para enviarle descargas desde el navegador."
        color: Theme.muted
        wrapMode: Text.Wrap
    }

    SettingRow {
        visible: !page.remote.registered
        label: "Nombre de este PC"

        TextField {
            id: deviceName
            Layout.fillWidth: true
            Layout.maximumWidth: page.controlWidth
            text: "Mi PC"
            maximumLength: 80
            Accessible.name: "Nombre de este PC"
            onAccepted: if (activate.enabled) activate.clicked()
        }
        Button {
            id: activate
            text: "Activar vinculación"
            enabled: !page.remote.busy && deviceName.text.trim().length > 0
            onClicked: page.remote.registerDevice(deviceName.text)
        }
    }

    SettingRow {
        visible: page.remote.registered
        label: "Este PC"

        Label {
            Layout.fillWidth: true
            text: page.remote.deviceName
            elide: Text.ElideRight
        }
    }

    SettingRow {
        visible: page.remote.registered
        label: "Navegadores"

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            // While the list is read, it is not yet empty.
            BusyIndicator {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                visible: page.remote.busy && page.remote.links.length === 0
                running: visible
            }
            Label {
                visible: !page.remote.busy && page.remote.links.length === 0
                text: "Ninguno"
                color: Theme.muted
            }

            Repeater {
                model: page.remote.links

                ColumnLayout {
                    id: link

                    required property var modelData
                    readonly property bool confirming: page.revokeId === modelData.id

                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: link.modelData.name + " · "
                                  + new Date(link.modelData.createdAt).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                            elide: Text.ElideRight
                        }
                        Button {
                            visible: !link.confirming
                            text: "Desvincular…"
                            enabled: !page.remote.busy
                            onClicked: page.revokeId = link.modelData.id
                            Accessible.name: "Desvincular " + link.modelData.name
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: link.confirming
                        text: "Las solicitudes pendientes de este navegador se cancelarán. "
                              + "Las ya recibidas seguirán en Descargas."
                        color: Theme.muted
                        wrapMode: Text.Wrap
                    }
                    // Windows order: the action, then Cancelar.
                    RowLayout {
                        visible: link.confirming
                        spacing: 8

                        Button {
                            text: "Desvincular"
                            enabled: !page.remote.busy
                            onClicked: {
                                page.remote.revokeLink(link.modelData.id)
                                page.revokeId = ""
                            }
                            Accessible.name: "Desvincular " + link.modelData.name
                        }
                        Button {
                            text: "Cancelar"
                            onClicked: page.revokeId = ""
                        }
                    }
                }
            }
        }
    }

    SettingRow {
        visible: page.remote.registered
        label: "Vincular otro"

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                visible: page.remote.code.length > 0
                text: page.remote.code
                font.pixelSize: Theme.title
                font.weight: Font.DemiBold
                font.letterSpacing: 2
                Accessible.name: "Código de vinculación: " + text
            }
            Label {
                Layout.fillWidth: true
                visible: page.remote.code.length > 0
                text: "Introdúcelo en la web de AnimeHub"
                      + (page.remote.expiresAt.length > 0
                         ? " antes de las " + new Date(page.remote.expiresAt).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                         : "") + "."
                color: Theme.muted
                wrapMode: Text.Wrap
            }
            Button {
                text: page.remote.code ? "Generar otro código" : "Generar código"
                enabled: !page.remote.busy
                onClicked: page.remote.generatePairingCode()
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.topMargin: 12
        visible: page.remote.error.length > 0
        text: page.remote.error
        color: Theme.danger
        wrapMode: Text.Wrap
    }
}
