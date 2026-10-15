import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AnimeHub

ModalDialog {
    id: dialog

    required property AppController app
    readonly property AppSettings settings: app.settings

    width: Math.min(620, parent.width - 48)
    height: Math.min(implicitHeight, parent.height - 48)
    title: "Ajustes"

    footer: DialogButtonBox {
        Button {
            text: "Cerrar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    component SectionTitle: Label {
        Layout.columnSpan: 2
        Layout.topMargin: 18
        color: Theme.ink
        font.pixelSize: Theme.body
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
    }

    component FieldLabel: Label {
        color: Theme.muted
        Layout.alignment: Qt.AlignVCenter
    }

    // A label whose ⓘ explains a consequence the control alone cannot show.
    component InfoLabel: RowLayout {
        property alias text: label.text
        property string info
        spacing: 6
        Layout.alignment: Qt.AlignVCenter

        Label {
            id: label
            color: Theme.muted
        }
        Glyph {
            glyph: Theme.icon.info
            color: Theme.muted
            HoverHandler { id: hover }
            ToolTip.visible: hover.hovered
            ToolTip.text: parent.info
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        // When the window is too short for every setting, the bar shows there is more below.
        ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        rightPadding: ScrollBar.vertical.policy === ScrollBar.AlwaysOn ? ScrollBar.vertical.width + 8 : 0

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 24
            rowSpacing: 10

            SectionTitle { text: "Descargas"; Layout.topMargin: 0 }

            FieldLabel { text: "Carpeta" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: dialog.settings.downloadDirectory.replace(/\//g, "\\")
                    color: Theme.ink
                    elide: Text.ElideMiddle
                    ToolTip.visible: folderArea.containsMouse
                    ToolTip.text: text
                    MouseArea { id: folderArea; anchors.fill: parent; hoverEnabled: true }
                }
                Button {
                    text: "Cambiar…"
                    onClicked: folderDialog.open()
                }
            }

            FieldLabel { text: "Servidor preferido" }
            ComboBox {
                Layout.fillWidth: true
                model: ["MEGA", "Pixeldrain"]
                currentIndex: dialog.settings.providerOrder[0] === "PIXELDRAIN" ? 1 : 0
                onActivated: index => dialog.settings.providerOrder = index === 1
                             ? ["PIXELDRAIN", "MEGA"] : ["MEGA", "PIXELDRAIN"]
                Accessible.name: "Servidor preferido"
            }

            FieldLabel { text: "Audio" }
            ComboBox {
                Layout.fillWidth: true
                model: ["Subtitulado", "Doblado"]
                currentIndex: dialog.settings.dubbed ? 1 : 0
                onActivated: index => dialog.settings.dubbed = index === 1
                Accessible.name: "Audio"
            }

            FieldLabel { text: "Descargas a la vez" }
            SpinBox {
                from: 1
                to: 4
                value: dialog.settings.maxConcurrent
                onValueModified: dialog.settings.maxConcurrent = value
                Accessible.name: "Descargas a la vez"
            }

            SectionTitle { text: "Reproducción" }

            InfoLabel {
                text: "Reproductor"
                info: "El reproductor incluido guarda el minuto en el que te quedas. Con la "
                      + "aplicación de Windows, abrir un episodio lo marca como visto."
            }
            ComboBox {
                Layout.fillWidth: true
                model: ["Incluido (mpv)", "Aplicación predeterminada de Windows"]
                currentIndex: dialog.settings.systemPlayer ? 1 : 0
                onActivated: index => dialog.settings.systemPlayer = index === 1
                Accessible.name: "Reproductor"
            }

            SectionTitle { text: "General" }

            FieldLabel { text: "Avisos" }
            Switch {
                leftPadding: 0 // its knob lines up with the controls above
                text: "Episodios nuevos de la biblioteca"
                checked: dialog.settings.notifications
                onToggled: dialog.settings.notifications = checked
            }

            FieldLabel { text: "Al iniciar Windows" }
            Switch {
                leftPadding: 0
                text: "Abrir AnimeHub en la bandeja"
                checked: dialog.settings.startWithWindows
                onToggled: dialog.settings.startWithWindows = checked
            }

            FieldLabel { text: "Al cerrar la ventana" }
            ComboBox {
                Layout.fillWidth: true
                model: ["Preguntar", "Seguir en la bandeja", "Salir"]
                currentIndex: dialog.settings.closeAction
                onActivated: index => dialog.settings.closeAction = index
                Accessible.name: "Al cerrar la ventana"
            }

            SectionTitle { text: "JDownloader" }

            InfoLabel {
                text: "Enviar enlaces a"
                info: "Añade «Enviar a JDownloader» a cada episodio, con los enlaces de todos los "
                      + "servidores, también MP4Upload y 1Fichier."
            }
            ComboBox {
                Layout.fillWidth: true
                model: ["No enviar", "JDownloader en este equipo", "MyJDownloader"]
                currentIndex: dialog.settings.jdownloader
                onActivated: index => dialog.settings.jdownloader = index
                Accessible.name: "Enviar enlaces a"
            }

            FieldLabel {
                text: "Cuenta"
                visible: remote.visible
            }
            RowLayout {
                id: remote
                Layout.fillWidth: true
                visible: dialog.settings.jdownloader === AppSettings.Remote

                Label {
                    Layout.fillWidth: true
                    text: dialog.app.jdownloader.account || "Sin conectar"
                    color: dialog.app.jdownloader.account ? Theme.ink : Theme.muted
                    elide: Text.ElideRight
                }
                BusyIndicator {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    running: dialog.app.jdownloader.busy
                    visible: running
                }
                Button {
                    text: dialog.app.jdownloader.account ? "Desconectar" : "Conectar…"
                    onClicked: dialog.app.jdownloader.account ? dialog.app.jdownloader.disconnectAccount()
                                                              : accountDialog.open()
                }
            }

            FieldLabel {
                text: "Equipo"
                visible: device.visible
            }
            ComboBox {
                id: device
                Layout.fillWidth: true
                visible: remote.visible && dialog.app.jdownloader.devices.length > 0
                textRole: "name"
                valueRole: "id"
                model: dialog.app.jdownloader.devices
                currentIndex: Math.max(0, indexOfValue(dialog.settings.jdownloaderDevice))
                onActivated: dialog.settings.jdownloaderDevice = currentValue
                Accessible.name: "Equipo con JDownloader"
            }

            Item {
                visible: jdProblem.visible
                Layout.preferredWidth: 1
            }
            Label {
                id: jdProblem
                Layout.fillWidth: true
                visible: remote.visible && dialog.app.jdownloader.problem !== ""
                text: dialog.app.jdownloader.problem
                color: Theme.danger
                wrapMode: Text.Wrap
            }

            Label {
                Layout.columnSpan: 2
                Layout.topMargin: 24
                text: "AnimeHub " + dialog.app.version
                color: Theme.faint
                font.pixelSize: Theme.caption
            }
        }
    }

    ModalDialog {
        id: accountDialog
        width: 420
        title: "Conectar MyJDownloader"
        onOpened: {
            password.text = ""
            email.forceActiveFocus()
        }
        onAccepted: dialog.app.jdownloader.connectAccount(email.text, password.text)

        ColumnLayout {
            width: 372
            spacing: 10

            TextField {
                id: email
                Layout.fillWidth: true
                placeholderText: "Correo"
                inputMethodHints: Qt.ImhEmailCharactersOnly
                text: dialog.app.jdownloader.account
                Accessible.name: "Correo de MyJDownloader"
            }
            TextField {
                id: password
                Layout.fillWidth: true
                placeholderText: "Contraseña"
                echoMode: TextInput.Password
                Accessible.name: "Contraseña de MyJDownloader"
                onAccepted: accountDialog.accept()
            }
        }

        footer: DialogButtonBox {
            Button {
                text: "Conectar"
                highlighted: true
                enabled: email.text.trim() !== "" && password.text !== ""
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Carpeta de descargas"
        currentFolder: "file:///" + dialog.settings.downloadDirectory
        onAccepted: dialog.settings.downloadDirectory = dialog.app.localPath(selectedFolder)
    }
}
