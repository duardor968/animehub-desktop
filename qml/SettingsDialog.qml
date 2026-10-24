import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AnimeHub

// Ajustes by category, as in Steam's and Windows' settings: the categories on the left, one page
// on the right. Every change applies at once; Esc or the close button leave.
ModalDialog {
    id: dialog
    objectName: "settingsDialog"

    required property AppController app
    readonly property AppSettings settings: app.settings
    readonly property JDownloaderView jd: app.jdownloader

    // JDownloader takes part when it downloads, or when episodes send it their links.
    readonly property bool jdEngine: settings.preferredJDownloader
    readonly property bool jdUsed: jdEngine || settings.jdownloader !== AppSettings.Off
    readonly property bool jdAccount: (jdEngine && settings.cloudJDownloader)
                                      || settings.jdownloader === AppSettings.Remote

    // Shipped next to AnimeHub.exe by the installer and the portable zip, not by a build.
    readonly property url licenseFile: settings.bundledFile("LICENSE.txt")
    readonly property url noticeFile: settings.bundledFile("THIRD-PARTY.txt")

    readonly property int controlWidth: 320
    readonly property var categories: [
        { key: "downloads", name: "Descargas" },
        { key: "playback", name: "Reproducción" },
        { key: "general", name: "General" },
        { key: "jdownloader", name: "JDownloader" },
        { key: "linked-devices", name: "Dispositivos vinculados" },
        { key: "about", name: "Acerca de" }
    ]

    // Opens on the category with that key; false when there is none.
    function openAt(key) {
        const index = categories.findIndex(category => category.key === key)
        if (index < 0)
            return false
        nav.currentIndex = index
        open()
        return true
    }

    // Clear of the tab bar above it, and of the window's edges down to its minimum size.
    width: Math.min(900, parent.width - 96)
    height: Math.min(600, parent.height - 120)
    padding: 0
    topPadding: 0
    bottomPadding: 0
    title: "Ajustes"
    header: null
    footer: null
    focus: true
    onOpened: nav.forceActiveFocus()

    // A category's rows; only the page scrolls when the dialog is shorter than they are.
    component SettingsPage: ScrollView {
        id: page

        default property alias rows: column.data

        contentWidth: availableWidth
        clip: true
        leftPadding: 32
        rightPadding: ScrollBar.vertical.policy === ScrollBar.AlwaysOn ? ScrollBar.vertical.width + 16 : 32
        bottomPadding: 24
        ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

        ColumnLayout {
            id: column
            width: page.availableWidth
            spacing: 0
        }
    }

    // Where episodes send their links; first while AnimeHub downloads, last once JDownloader does.
    component SendLinksRow: SettingRow {
        label: "Enviar enlaces a"
        info: "Añade «Enviar a JDownloader» a cada episodio, con los enlaces de todos los "
              + "servidores, también MP4Upload y 1Fichier."

        ComboBox {
            Layout.fillWidth: true
            Layout.maximumWidth: dialog.controlWidth
            model: ["No enviar", "JDownloader en este PC", "MyJDownloader"]
            currentIndex: dialog.settings.jdownloader
            onActivated: index => dialog.settings.jdownloader = index
            Accessible.name: "Enviar enlaces a"
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The categories, on the dialog's lower layer as Fluent draws its button band.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            Layout.topMargin: 1
            Layout.bottomMargin: 1
            Layout.leftMargin: 1
            color: dialog.palette.window
            topLeftRadius: 7
            bottomLeftRadius: 7

            ListView {
                id: nav
                objectName: "settingsCategories"

                anchors.fill: parent
                anchors.margins: 8
                anchors.topMargin: 22
                model: dialog.categories
                interactive: false
                keyNavigationEnabled: true
                activeFocusOnTab: true
                focus: true
                Accessible.role: Accessible.List
                Accessible.name: "Categorías"

                delegate: ItemDelegate {
                    required property int index
                    required property var modelData

                    width: ListView.view.width
                    text: modelData.name
                    highlighted: ListView.isCurrentItem
                    // The list holds the focus for the arrow keys; Tab goes on to the page.
                    focus: ListView.isCurrentItem
                    focusPolicy: Qt.ClickFocus
                    onClicked: nav.currentIndex = index
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 1
                color: Theme.stroke
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.bottomMargin: 12
                Layout.leftMargin: 32
                Layout.rightMargin: 56 // clear of the close button
                text: dialog.categories[nav.currentIndex].name
                color: Theme.ink
                font.pixelSize: Theme.title
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }

            StackLayout {
                id: pages
                objectName: "settingsPages"

                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: nav.currentIndex

                SettingsPage {
                    SettingRow {
                        label: "Descargar con"
                        info: "AnimeHub descarga de MEGA y Pixeldrain. JDownloader descarga también de "
                              + "MP4Upload y 1Fichier, y su progreso se ve igual en Descargas."

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            model: ["AnimeHub", "JDownloader"]
                            currentIndex: dialog.jdEngine ? 1 : 0
                            onActivated: index => dialog.settings.preferredJDownloader = index === 1
                            Accessible.name: "Descargar con"
                        }
                    }

                    SettingRow {
                        label: "Carpeta"

                        Label {
                            Layout.fillWidth: true
                            text: dialog.settings.downloadDirectory.replace(/\//g, "\\")
                            elide: Text.ElideMiddle
                            ToolTip.visible: folderHover.hovered && truncated
                            ToolTip.text: text
                            HoverHandler { id: folderHover }
                        }
                        Button {
                            text: "Cambiar…"
                            // A profile opened with --data-dir keeps its downloads inside it.
                            enabled: !dialog.settings.isolated
                            onClicked: folderDialog.open()
                            Accessible.name: "Cambiar la carpeta de descargas"
                        }
                    }

                    SettingRow {
                        label: "Servidor preferido"

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            model: ["MEGA", "Pixeldrain"]
                            currentIndex: dialog.settings.providerOrder[0] === "PIXELDRAIN" ? 1 : 0
                            onActivated: index => dialog.settings.providerOrder = index === 1
                                         ? ["PIXELDRAIN", "MEGA"] : ["MEGA", "PIXELDRAIN"]
                            Accessible.name: "Servidor preferido"
                        }
                    }

                    SettingRow {
                        label: "Audio"

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            model: ["Subtitulado", "Doblado"]
                            currentIndex: dialog.settings.dubbed ? 1 : 0
                            onActivated: index => dialog.settings.dubbed = index === 1
                            Accessible.name: "Audio"
                        }
                    }

                    SettingRow {
                        label: "Descargas a la vez"

                        SpinBox {
                            from: 1
                            to: 4
                            value: dialog.settings.maxConcurrent
                            onValueModified: dialog.settings.maxConcurrent = value
                            Accessible.name: "Descargas a la vez"
                        }
                    }
                }

                SettingsPage {
                    SettingRow {
                        label: "Reproductor"
                        info: "El reproductor incluido guarda el minuto en el que te quedas. Con la "
                              + "aplicación de Windows, abrir un episodio lo marca como visto."

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            model: ["Incluido (mpv)", "Aplicación predeterminada de Windows"]
                            currentIndex: dialog.settings.systemPlayer ? 1 : 0
                            onActivated: index => dialog.settings.systemPlayer = index === 1
                            Accessible.name: "Reproductor"
                        }
                    }
                }

                SettingsPage {
                    SettingRow {
                        label: "Avisos"

                        Switch {
                            leftPadding: 0 // its knob lines up with the controls of other rows
                            text: "Episodios nuevos de la biblioteca"
                            checked: dialog.settings.notifications
                            onToggled: dialog.settings.notifications = checked
                        }
                    }

                    SettingRow {
                        label: "Al iniciar Windows"

                        Switch {
                            leftPadding: 0
                            text: "Abrir AnimeHub en la bandeja"
                            // A profile opened with --data-dir never starts with Windows.
                            enabled: !dialog.settings.isolated
                            checked: dialog.settings.startWithWindows
                            onToggled: dialog.settings.startWithWindows = checked
                        }
                    }

                    SettingRow {
                        label: "Al cerrar la ventana"

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            model: ["Preguntar", "Seguir en la bandeja", "Salir"]
                            currentIndex: dialog.settings.closeAction
                            onActivated: index => dialog.settings.closeAction = index
                            Accessible.name: "Al cerrar la ventana"
                        }
                    }
                }

                SettingsPage {
                    Label {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 8
                        visible: !dialog.jdEngine
                        text: "Para MP4Upload y 1Fichier, los servidores que AnimeHub no descarga."
                        color: Theme.muted
                        wrapMode: Text.Wrap
                    }

                    SendLinksRow { visible: !dialog.jdEngine }

                    SettingRow {
                        visible: dialog.jdEngine
                        label: "Conexión"

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            model: ["Este PC", "MyJDownloader"]
                            currentIndex: dialog.settings.cloudJDownloader ? 1 : 0
                            onActivated: index => dialog.settings.cloudJDownloader = index === 1
                            Accessible.name: "Conexión con JDownloader"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdEngine && !dialog.settings.cloudJDownloader
                        label: "API local"

                        TextField {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            text: dialog.settings.jdEndpoint
                            onEditingFinished: {
                                dialog.settings.jdEndpoint = text
                                text = dialog.settings.jdEndpoint
                            }
                            Accessible.name: "URL de la API local de JDownloader"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdAccount
                        label: "Cuenta"

                        Label {
                            Layout.fillWidth: true
                            visible: dialog.jd.account !== ""
                            text: dialog.jd.account
                            elide: Text.ElideRight
                        }
                        // Signed out: the sign-in, in place.
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: dialog.jd.account === ""
                            spacing: 8

                            TextField {
                                id: email
                                Layout.fillWidth: true
                                Layout.maximumWidth: dialog.controlWidth
                                placeholderText: "Correo"
                                inputMethodHints: Qt.ImhEmailCharactersOnly
                                Accessible.name: "Correo de MyJDownloader"
                            }
                            TextField {
                                id: password
                                Layout.fillWidth: true
                                Layout.maximumWidth: dialog.controlWidth
                                placeholderText: "Contraseña"
                                echoMode: TextInput.Password
                                Accessible.name: "Contraseña de MyJDownloader"
                                onAccepted: if (signIn.enabled) signIn.clicked()
                            }
                            Button {
                                id: signIn
                                text: "Conectar"
                                enabled: !dialog.jd.busy && email.text.trim() !== "" && password.text !== ""
                                onClicked: {
                                    dialog.jd.connectAccount(email.text, password.text)
                                    password.text = ""
                                }
                                Accessible.name: "Conectar MyJDownloader"
                            }
                        }
                        BusyIndicator {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            running: dialog.jd.busy
                            visible: running
                        }
                        Button {
                            visible: dialog.jd.account !== ""
                            text: "Desconectar"
                            onClicked: dialog.jd.disconnectAccount()
                            Accessible.name: "Desconectar MyJDownloader"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdAccount && dialog.jd.devices.length > 0
                        label: "Equipo"

                        ComboBox {
                            Layout.fillWidth: true
                            Layout.maximumWidth: dialog.controlWidth
                            textRole: "name"
                            valueRole: "id"
                            model: dialog.jd.devices
                            currentIndex: Math.max(0, indexOfValue(dialog.settings.jdownloaderDevice))
                            onActivated: dialog.settings.jdownloaderDevice = currentValue
                            Accessible.name: "Equipo con JDownloader"
                        }
                    }

                    SendLinksRow { visible: dialog.jdEngine }

                    SettingRow {
                        visible: dialog.jdUsed
                        label: "Aplicación"

                        Label {
                            Layout.fillWidth: true
                            text: dialog.settings.jdExecutable || "Sin elegir"
                            color: dialog.settings.jdExecutable ? Theme.ink : Theme.muted
                            elide: Text.ElideMiddle
                            ToolTip.visible: appHover.hovered && truncated
                            ToolTip.text: text
                            HoverHandler { id: appHover }
                        }
                        Button {
                            text: "Elegir…"
                            onClicked: jdFile.open()
                            Accessible.name: "Elegir la aplicación JDownloader"
                        }
                        Button {
                            visible: dialog.settings.jdExecutable !== ""
                            text: "Iniciar"
                            onClicked: dialog.jd.launch()
                            Accessible.name: "Iniciar JDownloader"
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        visible: dialog.jdUsed && dialog.jd.problem !== ""
                        text: dialog.jd.problem
                        color: Theme.danger
                        wrapMode: Text.Wrap
                    }
                }

                SettingsPage {
                    // The browsers linked to this PC change elsewhere: read again on each visit.
                    readonly property bool showing: StackLayout.isCurrentItem && dialog.visible
                    onShowingChanged: if (showing) linkedDevices.refresh()

                    LinkedDevicesPage {
                        id: linkedDevices
                        Layout.fillWidth: true
                        remote: dialog.app.remote
                        controlWidth: dialog.controlWidth
                    }
                }

                SettingsPage {
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 16
                        spacing: 16

                        Image {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            source: "qrc:/qt/qml/AnimeHub/assets/animehub.png"
                            mipmap: true
                            Accessible.ignored: true
                        }
                        ColumnLayout {
                            spacing: 2

                            Label {
                                text: "AnimeHub"
                                color: Theme.ink
                                font.pixelSize: Theme.subtitle
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: "Versión " + dialog.app.version
                                color: Theme.muted
                            }
                        }
                    }

                    SettingRow {
                        label: "Licencia"

                        Label {
                            Layout.fillWidth: true
                            text: "GNU GPL 3.0 o posterior"
                        }
                        Button {
                            visible: dialog.licenseFile.toString() !== ""
                            text: "Abrir"
                            onClicked: dialog.app.openUrl(dialog.licenseFile)
                            Accessible.name: "Abrir la licencia"
                        }
                    }

                    SettingRow {
                        visible: dialog.noticeFile.toString() !== ""
                        label: "Software de terceros"

                        Label {
                            Layout.fillWidth: true
                            text: "Qt, aria2 y mpv"
                        }
                        Button {
                            text: "Abrir"
                            onClicked: dialog.app.openUrl(dialog.noticeFile)
                            Accessible.name: "Abrir los avisos de software de terceros"
                        }
                    }
                }
            }
        }
    }

    // Declared after the pages: last in the Tab order.
    ToolButton {
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 8
        text: "Cerrar"
        display: AbstractButton.IconOnly
        icon.source: Theme.source(Theme.icon.cancel)
        ToolTip.visible: hovered
        ToolTip.text: "Cerrar (Esc)"
        onClicked: dialog.close()
    }

    FolderDialog {
        id: folderDialog
        title: "Carpeta de descargas"
        currentFolder: "file:///" + dialog.settings.downloadDirectory
        onAccepted: dialog.settings.downloadDirectory = dialog.app.localPath(selectedFolder)
    }
    FileDialog {
        id: jdFile
        title: "Elegir JDownloader"
        nameFilters: ["Aplicaciones (*.exe)"]
        onAccepted: dialog.settings.jdExecutable = dialog.app.localPath(selectedFile)
    }
}
