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

    // JDownloader takes part when it downloads, or, while AnimeHub does, when episodes send it
    // the links of the servers AnimeHub does not download.
    readonly property bool jdEngine: settings.preferredJDownloader
    readonly property bool jdUsed: jdEngine || settings.jdownloader !== AppSettings.Off
    readonly property bool jdAccount: jdEngine ? settings.cloudJDownloader
                                               : settings.jdownloader === AppSettings.Remote

    // Shipped next to AnimeHub.exe by the installer and the portable zip, not by a build.
    readonly property url licenseFile: settings.bundledFile("LICENSE.txt")
    readonly property url noticeFile: settings.bundledFile("THIRD-PARTY.txt")
    // A profile opened with --data-dir keeps its downloads inside it and never starts with Windows.
    readonly property string isolatedReason: settings.isolated
                                             ? "No disponible en un perfil de prueba" : ""

    readonly property int controlWidth: 300
    readonly property var categories: [
        { key: "downloads", name: "Descargas" },
        { key: "general", name: "General" },
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

    // Lists and text fields share one width, against the rows' right edge.
    component Choice: ComboBox {
        Layout.fillWidth: true
        Layout.preferredWidth: dialog.controlWidth
        Layout.maximumWidth: dialog.controlWidth
    }
    component Field: TextField {
        Layout.fillWidth: true
        Layout.preferredWidth: dialog.controlWidth
        Layout.maximumWidth: dialog.controlWidth
    }
    // A switch as Windows' settings show one: its text, then the knob against the right edge.
    component Toggle: RowLayout {
        id: toggle

        property alias text: caption.text
        property alias checked: knob.checked
        signal toggled

        spacing: 12

        Label {
            id: caption
            Accessible.ignored: true
            // As a switch's own text, it switches it too.
            TapHandler {
                onTapped: {
                    knob.toggle()
                    knob.toggled()
                }
            }
        }
        Switch {
            id: knob
            // Without a text, as wide as its knob: flush with the edge.
            Layout.preferredWidth: implicitIndicatorWidth
            Accessible.name: caption.text
            onToggled: toggle.toggled()
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

                        Choice {
                            model: ["AnimeHub", "JDownloader"]
                            currentIndex: dialog.jdEngine ? 1 : 0
                            onActivated: index => dialog.settings.preferredJDownloader = index === 1
                            Accessible.name: "Descargar con"
                        }
                    }

                    // Only while AnimeHub downloads: JDownloader, as the engine, takes them all.
                    SettingRow {
                        visible: !dialog.jdEngine
                        label: "Servidores que AnimeHub no descarga (MP4Upload, 1Fichier)"
                        info: "Añade «Enviar a JDownloader» a cada episodio, con los enlaces de todos los "
                              + "servidores, también MP4Upload y 1Fichier."

                        Choice {
                            model: ["No enviar", "Enviar a JDownloader en este PC",
                                    "Enviar por MyJDownloader"]
                            currentIndex: dialog.settings.jdownloader
                            onActivated: index => dialog.settings.jdownloader = index
                            Accessible.name: "Servidores que AnimeHub no descarga"
                        }
                    }

                    // How to reach JDownloader, only while it takes part.
                    SettingRow {
                        visible: dialog.jdEngine
                        label: "Conexión con JDownloader"

                        Choice {
                            model: ["Este PC", "MyJDownloader"]
                            currentIndex: dialog.settings.cloudJDownloader ? 1 : 0
                            onActivated: index => dialog.settings.cloudJDownloader = index === 1
                            Accessible.name: "Conexión con JDownloader"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdEngine && !dialog.settings.cloudJDownloader
                        label: "API local"

                        Field {
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
                        label: "Cuenta de MyJDownloader"
                        value: dialog.jd.account

                        // Signed out: the sign-in, in place.
                        ColumnLayout {
                            visible: dialog.jd.account === ""
                            spacing: 8

                            Field {
                                id: email
                                placeholderText: "Correo"
                                inputMethodHints: Qt.ImhEmailCharactersOnly
                                Accessible.name: "Correo de MyJDownloader"
                            }
                            Field {
                                id: password
                                placeholderText: "Contraseña"
                                echoMode: TextInput.Password
                                Accessible.name: "Contraseña de MyJDownloader"
                                onAccepted: if (signIn.enabled) signIn.clicked()
                            }
                            Button {
                                id: signIn
                                Layout.alignment: Qt.AlignRight
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

                        Choice {
                            textRole: "name"
                            valueRole: "id"
                            model: dialog.jd.devices
                            currentIndex: Math.max(0, indexOfValue(dialog.settings.jdownloaderDevice))
                            onActivated: dialog.settings.jdownloaderDevice = currentValue
                            Accessible.name: "Equipo con JDownloader"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdUsed
                        label: "Aplicación JDownloader"
                        value: dialog.settings.jdExecutable || "Sin elegir"
                        valueColor: dialog.settings.jdExecutable ? Theme.ink : Theme.muted

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
                        Layout.bottomMargin: 12
                        visible: dialog.jdUsed && dialog.jd.problem !== ""
                        text: dialog.jd.problem
                        color: Theme.danger
                        wrapMode: Text.Wrap
                    }

                    SettingRow {
                        label: "Carpeta"
                        value: dialog.settings.downloadDirectory.replace(/\//g, "\\")
                        unavailable: dialog.isolatedReason

                        Button {
                            text: "Cambiar…"
                            onClicked: folderDialog.open()
                            Accessible.name: "Cambiar la carpeta de descargas"
                        }
                    }

                    SettingRow {
                        label: "Servidor preferido"

                        Choice {
                            model: ["MEGA", "Pixeldrain"]
                            currentIndex: dialog.settings.providerOrder[0] === "PIXELDRAIN" ? 1 : 0
                            onActivated: index => dialog.settings.providerOrder = index === 1
                                         ? ["PIXELDRAIN", "MEGA"] : ["MEGA", "PIXELDRAIN"]
                            Accessible.name: "Servidor preferido"
                        }
                    }

                    SettingRow {
                        label: "Audio"

                        Choice {
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

                        Choice {
                            model: ["Incluido (mpv)", "Aplicación predeterminada de Windows"]
                            currentIndex: dialog.settings.systemPlayer ? 1 : 0
                            onActivated: index => dialog.settings.systemPlayer = index === 1
                            Accessible.name: "Reproductor"
                        }
                    }

                    SettingRow {
                        label: "Avisos"

                        Toggle {
                            text: "Episodios nuevos de la biblioteca"
                            checked: dialog.settings.notifications
                            onToggled: dialog.settings.notifications = checked
                        }
                    }

                    SettingRow {
                        label: "Al iniciar Windows"

                        unavailable: dialog.isolatedReason
                        Toggle {
                            text: "Abrir AnimeHub en la bandeja"
                            checked: dialog.settings.startWithWindows
                            onToggled: dialog.settings.startWithWindows = checked
                        }
                    }

                    SettingRow {
                        label: "Al cerrar la ventana"

                        Choice {
                            model: ["Preguntar", "Seguir en la bandeja", "Salir"]
                            currentIndex: dialog.settings.closeAction
                            onActivated: index => dialog.settings.closeAction = index
                            Accessible.name: "Al cerrar la ventana"
                        }
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
                        value: "GNU GPL 3.0 o posterior"

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
                        value: "Qt, aria2 y mpv"

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
