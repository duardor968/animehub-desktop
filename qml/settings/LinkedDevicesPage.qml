import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Ajustes › Dispositivos vinculados: this PC joins the AnimeHub website, which can then send it
// downloads from any browser linked to it. Unlinking one is confirmed in its own line. While the
// page is on view it reads the list again every few seconds: a browser that links with the code
// shows at once, and the spent code goes.
ColumnLayout {
    id: page

    required property RemoteView remote
    property real controlWidth: 320
    // The page is on view.
    property bool active: false
    // The browser whose unlinking waits for confirmation.
    property string revokeId: ""
    // A browser linked with the code on view, since it was generated.
    property bool justLinked: false

    // Each time the page comes into view.
    function refresh() {
        revokeId = ""
        remote.refresh()
    }

    Timer {
        interval: 3000
        repeat: true
        running: page.active && page.remote.registered
        onTriggered: page.remote.watch()
    }

    Connections {
        target: page.remote
        function onLinked() { page.justLinked = true }
    }

    spacing: 0

    SettingRow {
        visible: !page.remote.registered
        label: "Nombre de este PC"
        info: "Vincula este PC con la web de AnimeHub para enviarle descargas desde el navegador."

        // The field in Ajustes' column of lists and fields, its button under it, as MyJDownloader's
        // sign-in.
        ColumnLayout {
            spacing: 8

            TextField {
                id: deviceName
                Layout.fillWidth: true
                Layout.preferredWidth: page.controlWidth
                Layout.maximumWidth: page.controlWidth
                text: page.remote.suggestedName
                maximumLength: 80
                Accessible.name: "Nombre de este PC"
                onAccepted: if (activate.enabled) activate.clicked()
            }
            Button {
                id: activate
                Layout.alignment: Qt.AlignRight
                text: "Activar vinculación"
                enabled: !page.remote.busy && deviceName.text.trim().length > 0
                onClicked: page.remote.registerDevice(deviceName.text)
            }
        }
    }

    SettingRow {
        visible: page.remote.registered
        label: "Este PC"
        value: page.remote.deviceName
    }

    SettingRow {
        visible: page.remote.registered
        label: "Navegadores"

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            // While the list is read, it is not yet empty.
            BusyIndicator {
                Layout.alignment: Qt.AlignRight
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                visible: page.remote.busy && page.remote.links.length === 0
                running: visible
            }
            Label {
                Layout.alignment: Qt.AlignRight
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
                            text: link.modelData.name + " · desde el " + link.modelData.since
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
                        Layout.alignment: Qt.AlignRight
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
        id: pairing
        visible: page.remote.registered
        label: "Vincular otro"

        ColumnLayout {
            id: pairingColumn

            // Under its name in a narrow window, it starts at the left.
            readonly property int side: pairing.stacked ? Qt.AlignLeft : Qt.AlignRight

            Layout.fillWidth: true
            spacing: 8

            // The code and the way to copy it, side by side.
            RowLayout {
                Layout.alignment: pairingColumn.side
                visible: page.remote.code.length > 0
                spacing: 8

                Label {
                    text: page.remote.code
                    color: Theme.ink
                    font.pixelSize: Theme.title
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                    Accessible.name: "Código de vinculación: " + text
                }
                ToolButton {
                    id: copy
                    property bool copied: false
                    text: copied ? "Copiado" : "Copiar el código"
                    display: AbstractButton.IconOnly
                    icon.source: Theme.source(copied ? Theme.icon.check : "e8c8")
                    ToolTip.visible: hovered || copied
                    ToolTip.text: text
                    onClicked: {
                        page.remote.copyCode()
                        copied = true
                        copiedTimer.restart()
                    }
                    Timer {
                        id: copiedTimer
                        interval: 1500
                        onTriggered: copy.copied = false
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: page.remote.code.length > 0
                text: "Introdúcelo en la web de AnimeHub"
                      + (page.remote.expiresText.length > 0 ? " antes de las " + page.remote.expiresText : "")
                color: Theme.muted
                horizontalAlignment: pairing.stacked ? Text.AlignLeft : Text.AlignRight
                wrapMode: Text.Wrap
            }
            // The code was used: it goes, and this says why.
            Label {
                Layout.alignment: pairingColumn.side
                visible: page.justLinked && page.remote.code.length === 0
                text: "Navegador vinculado"
                color: Theme.success
            }
            Button {
                Layout.alignment: pairingColumn.side
                text: page.remote.code ? "Generar otro código" : "Generar código"
                enabled: !page.remote.busy
                onClicked: {
                    page.justLinked = false
                    page.remote.generatePairingCode()
                }
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
