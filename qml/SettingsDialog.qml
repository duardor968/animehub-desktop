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

    // How JDownloader is reached, if it is used at all.
    readonly property bool jdUsed: settings.jdownloader !== AppSettings.Off
    readonly property bool jdAccount: settings.jdownloader === AppSettings.Remote
    readonly property bool jdLocal: settings.jdownloader === AppSettings.Local

    // Shipped next to AnimeHub.exe by the installer and the portable zip, not by a build.
    readonly property url licenseFile: settings.bundledFile("LICENSE.txt")
    readonly property url noticeFile: settings.bundledFile("THIRD-PARTY.txt")
    // A profile opened with --data-dir keeps its downloads inside it and never starts with Windows.
    readonly property string isolatedReason: settings.isolated
                                             ? "No disponible en un perfil de prueba" : ""

    readonly property int controlWidth: 320
    // Lists and text fields share one width across Ajustes: the widest list's, when its longest
    // option needs more than controlWidth.
    property real widestChoice: controlWidth
    readonly property real fieldWidth: Math.max(controlWidth, widestChoice)
    // Linux has no bundled mpv (the system's is used) and its desktops name things otherwise.
    readonly property bool windows: Qt.platform.os === "windows"
    readonly property var categories: [
        { key: "general", name: "General" },
        { key: "downloads", name: "Descargas" },
        { key: "linked-devices", name: "Dispositivos vinculados" },
        { key: "about", name: "Acerca de" }
    ]

    // A path as the system writes it: every path in Ajustes reads one way.
    function nativePath(path) {
        return windows ? path.replace(/\//g, "\\") : path
    }

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
        // The scroll bar lies in this margin: the rows keep one right edge on every page, with a
        // bar or without.
        leftPadding: 32
        rightPadding: 32
        bottomPadding: 24
        ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

        ColumnLayout {
            id: column
            width: page.availableWidth
            spacing: 0
        }
    }

    // Lists and text fields share one width, against the rows' right edge, so their edges and
    // chevrons line up; no option is ever cut.
    component Choice: ComboBox {
        implicitContentWidthPolicy: ComboBox.WidestText
        Layout.fillWidth: true
        Layout.preferredWidth: dialog.fieldWidth
        Layout.maximumWidth: dialog.fieldWidth
        onImplicitWidthChanged: dialog.widestChoice = Math.max(dialog.widestChoice, implicitWidth)
        Component.onCompleted: dialog.widestChoice = Math.max(dialog.widestChoice, implicitWidth)
    }
    component Field: TextField {
        Layout.fillWidth: true
        Layout.preferredWidth: dialog.fieldWidth
        Layout.maximumWidth: dialog.fieldWidth
    }
    // A switch as Windows' settings show one: the row's name says what it does, the knob sits
    // against the right edge with no text of its own. `name` is that row's name, for a reader.
    component Toggle: Switch {
        property string name
        // Without a text, as wide as its knob: flush with the edge.
        Layout.preferredWidth: implicitIndicatorWidth
        Accessible.name: name
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
                        label: "Reproductor"
                        info: dialog.windows
                              ? "El reproductor incluido y PotPlayer guardan el minuto en el que te "
                                + "quedas, y PotPlayer sigue con los siguientes episodios descargados. "
                                + "Con otra aplicación de Windows, abrir un episodio lo marca como visto."
                              : "mpv guarda el minuto en el que te quedas. Con la aplicación "
                                + "predeterminada, abrir un episodio lo marca como visto."

                        // PotPlayer only while it is installed.
                        Choice {
                            readonly property var keys: dialog.settings.potPlayerFound
                                                        ? ["mpv", "potplayer", "system"]
                                                        : ["mpv", "system"]
                            model: dialog.windows
                                   ? (dialog.settings.potPlayerFound
                                      ? ["Incluido (mpv)", "PotPlayer", "Aplicación predeterminada de Windows"]
                                      : ["Incluido (mpv)", "Aplicación predeterminada de Windows"])
                                   : ["mpv", "Aplicación predeterminada"]
                            currentIndex: Math.max(0, keys.indexOf(dialog.settings.player))
                            onActivated: index => dialog.settings.player = keys[index]
                            Accessible.name: "Reproductor"
                        }
                    }

                    SettingRow {
                        id: notifyRow
                        label: "Avisar de episodios nuevos"

                        Toggle {
                            name: notifyRow.label
                            checked: dialog.settings.notifications
                            onToggled: dialog.settings.notifications = checked
                        }
                    }

                    SettingRow {
                        id: startRow
                        // A desktop without a tray (GNOME without its extension) shows the window.
                        label: (dialog.app.tray ? "Abrir en la bandeja al iniciar " : "Abrir al iniciar ")
                               + (dialog.windows ? "Windows" : "sesión")
                        unavailable: dialog.isolatedReason

                        Toggle {
                            name: startRow.label
                            checked: dialog.settings.startWithWindows
                            onToggled: dialog.settings.startWithWindows = checked
                        }
                    }

                    // Without a tray, closing the window quits.
                    SettingRow {
                        visible: dialog.app.tray
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
                    // The path under the name: it has the row's width, and the button keeps its column.
                    SettingRow {
                        label: "Carpeta"
                        detail: dialog.nativePath(dialog.settings.downloadDirectory)
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
                            readonly property var keys: ["MEGA", "PIXELDRAIN", "TRANSFERIT"]
                            model: ["MEGA", "Pixeldrain", "transfer.it"]
                            currentIndex: Math.max(0, keys.indexOf(dialog.settings.providerOrder[0]))
                            onActivated: index => dialog.settings.providerOrder =
                                         [keys[index]].concat(keys.filter((key, at) => at !== index))
                            Accessible.name: "Servidor preferido"
                        }
                    }

                    // As the web's preferences: which servers episodes come from. Their knobs in
                    // one column, their names after them.
                    SettingRow {
                        label: "Proveedores"

                        ColumnLayout {
                            spacing: 8

                            Repeater {
                                model: dialog.settings.providerSwitches

                                // A switch that cannot be used says why on hover and when the
                                // keyboard reaches it: a disabled control takes no focus itself.
                                Item {
                                    id: provider
                                    required property var modelData
                                    readonly property bool explained: modelData.reason !== ""
                                    Layout.fillWidth: true
                                    implicitWidth: providerSwitch.implicitWidth
                                    implicitHeight: providerSwitch.implicitHeight
                                    activeFocusOnTab: !providerSwitch.enabled
                                    Accessible.role: Accessible.CheckBox
                                    Accessible.name: modelData.name
                                    Accessible.description: modelData.reason
                                    Accessible.checkable: true
                                    Accessible.checked: modelData.checked
                                    Accessible.ignored: providerSwitch.enabled
                                    HoverHandler { id: reasonHover }
                                    ToolTip.visible: explained && (reasonHover.hovered || activeFocus)
                                    ToolTip.text: modelData.reason

                                    Toggle {
                                        id: providerSwitch
                                        anchors.fill: parent
                                        name: provider.modelData.name
                                        text: provider.modelData.name
                                        enabled: provider.modelData.enabled
                                        checked: provider.modelData.checked
                                        onToggled: dialog.settings.setProviderEnabled(
                                                       provider.modelData.key, checked)

                                        FocusFrame {
                                            target: providerSwitch.indicator
                                            visible: provider.activeFocus
                                            radiusInside: providerSwitch.indicator.height / 2
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // MP4Upload and 1Fichier only download through it.
                    SettingRow {
                        label: "JDownloader"
                        info: "Descarga MP4Upload y 1Fichier, que AnimeHub no descarga, en la carpeta "
                              + "de AnimeHub; su progreso se ve igual en Descargas."

                        Choice {
                            model: ["No usar", "En este PC", "Por MyJDownloader"]
                            currentIndex: dialog.settings.jdownloader
                            onActivated: index => dialog.settings.jdownloader = index
                            Accessible.name: "JDownloader"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdUsed
                        label: "MEGA, Pixeldrain y transfer.it"
                        info: "AnimeHub los descarga por su cuenta. Con JDownloader, todas las "
                              + "descargas pasan por él."

                        Choice {
                            model: ["Con AnimeHub", "Con JDownloader"]
                            currentIndex: dialog.settings.preferredJDownloader ? 1 : 0
                            onActivated: index => dialog.settings.preferredJDownloader = index === 1
                            Accessible.name: "Descargar MEGA, Pixeldrain y transfer.it"
                        }
                    }

                    SettingRow {
                        visible: dialog.jdLocal
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

                    // The path under the name, as the folder's: the buttons keep the column.
                    SettingRow {
                        visible: dialog.jdUsed
                        label: "Aplicación JDownloader"
                        detail: dialog.settings.jdExecutable ? dialog.nativePath(dialog.settings.jdExecutable)
                                                             : "Sin elegir"

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
                    id: linkedDevicesPage
                    // The browsers linked to this PC change elsewhere: read again on each visit.
                    readonly property bool showing: StackLayout.isCurrentItem && dialog.visible
                    onShowingChanged: if (showing) linkedDevices.refresh()

                    LinkedDevicesPage {
                        id: linkedDevices
                        Layout.fillWidth: true
                        active: linkedDevicesPage.showing
                        remote: dialog.app.remote
                        controlWidth: dialog.fieldWidth
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
        // A Windows path starts with its drive, a Linux one with its "/".
        currentFolder: (dialog.windows ? "file:///" : "file://") + dialog.settings.downloadDirectory
        onAccepted: dialog.settings.downloadDirectory = dialog.app.localPath(selectedFolder)
    }
    FileDialog {
        id: jdFile
        title: "Elegir JDownloader"
        // On Linux JDownloader2 is a script without an extension.
        nameFilters: dialog.windows ? ["Aplicaciones (*.exe)"] : []
        onAccepted: dialog.settings.jdExecutable = dialog.app.localPath(selectedFile)
    }
}
