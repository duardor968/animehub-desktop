import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AnimeHub

Dialog {
    id: dialog

    required property AppController app
    readonly property AppSettings settings: app.settings

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, parent.width - 48)
    height: Math.min(implicitHeight, parent.height - 48)
    modal: true
    title: "Ajustes"
    standardButtons: Dialog.Close

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

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

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
                    text: dialog.settings.downloadDirectory
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

            FieldLabel { text: "Reproductor" }
            RowLayout {
                Layout.fillWidth: true

                ComboBox {
                    Layout.fillWidth: true
                    model: ["Incluido (mpv)", "Aplicación predeterminada de Windows"]
                    currentIndex: dialog.settings.systemPlayer ? 1 : 0
                    onActivated: index => dialog.settings.systemPlayer = index === 1
                    Accessible.name: "Reproductor"
                }

                Glyph {
                    glyph: Theme.icon.info
                    color: Theme.muted
                    HoverHandler { id: playerInfo }
                    ToolTip.visible: playerInfo.hovered
                    ToolTip.text: "El reproductor incluido guarda el minuto en el que te quedas. "
                                  + "Con la aplicación de Windows, abrir un episodio lo marca como visto."
                }
            }

            SectionTitle { text: "General" }

            FieldLabel { text: "Avisos" }
            Switch {
                text: "Episodios nuevos de la biblioteca"
                checked: dialog.settings.notifications
                onToggled: dialog.settings.notifications = checked
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

            FieldLabel { text: "Click'n'Load" }
            RowLayout {
                Switch {
                    text: "Enviar enlaces a JDownloader"
                    checked: dialog.settings.clickNLoad
                    onToggled: dialog.settings.clickNLoad = checked
                }

                Glyph {
                    glyph: Theme.icon.info
                    color: Theme.muted
                    HoverHandler { id: jdInfo }
                    ToolTip.visible: jdInfo.hovered
                    ToolTip.text: "Añade «Enviar a JDownloader» a cada episodio, con los enlaces de "
                                  + "todos los servidores, también MP4Upload y 1Fichier."
                }
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

    FolderDialog {
        id: folderDialog
        title: "Carpeta de descargas"
        currentFolder: "file:///" + dialog.settings.downloadDirectory
        onAccepted: dialog.settings.downloadDirectory = dialog.app.localPath(selectedFolder)
    }
}
