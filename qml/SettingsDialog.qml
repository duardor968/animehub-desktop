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

    readonly property int controlWidth: 240
    readonly property int maxControlWidth: 300
    // Lists and text fields share one capped width across Ajustes so long player or device names
    // never push controls off-screen at 820x600.
    property real widestChoice: controlWidth
    readonly property real fieldWidth: Math.min(maxControlWidth, Math.max(controlWidth, widestChoice))
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

    property bool diagnosticsCopied: false
    property bool aboutEnginesExpanded: false

    Timer {
        id: copiedTimer
        interval: 2000
        onTriggered: dialog.diagnosticsCopied = false
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
    onOpened: nav.forceActiveFocus(InputModality.reason)
    onClosed: {
        diagnosticsCopied = false
        aboutEnginesExpanded = false
        updateSection.notesExpanded = false
    }

    // Keep whatever the keyboard reaches inside the active settings ScrollView cleanly in view.
    Connections {
        target: dialog.ApplicationWindow.window
        function onActiveFocusItemChanged() {
            if (!dialog.visible)
                return
            const win = dialog.ApplicationWindow.window
            const item = win ? win.activeFocusItem : null
            if (pages.currentIndex === 0)
                generalPage.ensureVisible(item)
            else if (pages.currentIndex === 1)
                downloadsSettingsPage.ensureVisible(item)
            else if (pages.currentIndex === 2)
                linkedDevicesPage.ensureVisible(item)
            else if (pages.currentIndex === 3)
                aboutPage.ensureVisible(item)
        }
    }

    // A category's rows; only the page scrolls when the dialog is shorter than they are.
    component SettingsPage: ScrollView {
        id: page

        default property alias rows: column.data

        function ensureVisible(item) {
            if (!item || !page.visible || !InputModality.keyboard)
                return
            const flick = page.contentItem as Flickable
            if (!flick || flick.contentHeight <= flick.height)
                return
            let inside = false
            for (let p = item; p && !inside; p = p.parent)
                inside = p === column
            if (!inside)
                return
            const top = item.mapToItem(column, 0, 0).y
            if (top - 16 < flick.contentY)
                flick.contentY = Math.max(0, top - 16)
            else if (top + item.height + 16 > flick.contentY + flick.height)
                flick.contentY = Math.min(top + item.height + 16 - flick.height,
                                          Math.max(0, flick.contentHeight - flick.height))
        }

        contentWidth: availableWidth
        clip: true
        // The scroll bar lies in this margin: the rows keep one right edge on every page, with a
        // bar or without. Top padding keeps the first focused control's FocusFrame unclipped.
        topPadding: 4
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

    // Lists and text fields share one capped width, against the rows' right edge, so their edges
    // and chevrons line up while staying bounded at narrow widths.
    component Choice: ComboBox {
        implicitContentWidthPolicy: ComboBox.WidestText
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: dialog.fieldWidth
        Layout.maximumWidth: dialog.fieldWidth
        onImplicitWidthChanged: dialog.widestChoice = Math.min(dialog.maxControlWidth,
                                                               Math.max(dialog.widestChoice, implicitWidth))
        Component.onCompleted: dialog.widestChoice = Math.min(dialog.maxControlWidth,
                                                              Math.max(dialog.widestChoice, implicitWidth))
        ToolTip.visible: (hovered || (activeFocus && InputModality.keyboard))
                         && displayText !== "" && implicitWidth > width
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.timeout: Theme.tooltipTimeout
        ToolTip.text: displayText
    }
    component Field: TextField {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
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

    // Windows 11 SettingsCard surface for "Acerca de" (Files / PowerToys pattern).
    component SettingsCard: Rectangle {
        id: card
        default property alias body: cardBox.data
        property int verticalPadding: 12

        Layout.fillWidth: true
        implicitHeight: cardBox.implicitHeight + verticalPadding * 2
        radius: Theme.radius
        color: Qt.rgba(1, 1, 1, 0.045)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)

        ColumnLayout {
            id: cardBox
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: card.verticalPadding
            anchors.bottomMargin: card.verticalPadding
            spacing: 0
        }
    }

    component AboutLinkRow: AbstractButton {
        id: linkRow
        property string title
        property string subtitle

        Layout.fillWidth: true
        implicitHeight: 40
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        Keys.onReturnPressed: event => {
            event.accepted = true
            linkRow.click()
        }
        Keys.onEnterPressed: event => {
            event.accepted = true
            linkRow.click()
        }
        Accessible.role: Accessible.Link
        Accessible.name: title + (subtitle !== "" ? ", " + subtitle : "")
        Accessible.onPressAction: click()

        background: Rectangle {
            x: -8
            width: parent.width + 16
            radius: Theme.radiusSm
            color: linkRow.down ? Theme.pressSubtle
                   : linkRow.hovered ? Theme.hoverSubtle : "transparent"

            FocusFrame {
                visible: linkRow.activeFocus && InputModality.keyboard
                radiusInside: Theme.radiusSm
            }
        }

        contentItem: RowLayout {
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Label {
                    Layout.fillWidth: true
                    text: linkRow.title
                    color: Theme.ink
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: linkRow.subtitle
                    color: Theme.muted
                    font.pixelSize: Theme.micro
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
            }

            Glyph {
                glyph: Theme.icon.openExternal
                size: 12
                color: linkRow.hovered ? Theme.ink : Theme.muted
            }
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
                    id: navItem
                    required property int index
                    required property var modelData

                    width: ListView.view.width
                    text: modelData.name
                    highlighted: ListView.isCurrentItem
                    // The list holds the focus for the arrow keys; Tab goes on to the page.
                    focus: ListView.isCurrentItem
                    focusPolicy: Qt.ClickFocus
                    onClicked: nav.currentIndex = index

                    // Arrow-key navigation moves focus with OtherFocusReason (clearing visualFocus),
                    // so keep the keyboard FocusFrame visible across ↑/↓ moves without double-drawing.
                    FocusFrame {
                        visible: navItem.ListView.isCurrentItem && nav.activeFocus
                                 && InputModality.keyboard && !navItem.visualFocus
                        radiusInside: Theme.radiusSm
                    }
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
                    id: generalPage
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

                    SettingRow {
                        label: "Episodios en Descargas"

                        Choice {
                            readonly property var keys: ["cards", "compact"]
                            model: ["Con miniaturas", "Tabla compacta"]
                            currentIndex: Math.max(0, keys.indexOf(dialog.settings.downloadEpisodesStyle))
                            onActivated: index => dialog.settings.downloadEpisodesStyle = keys[index]
                            Accessible.name: "Episodios en Descargas"
                        }
                    }

                    SettingRow {
                        label: "Actualizaciones"
                        unavailable: dialog.isolatedReason

                        Choice {
                            model: ["Automáticas", "Solo avisar", "Manuales"]
                            currentIndex: dialog.settings.updateMode
                            onActivated: index => dialog.settings.updateMode = index
                            Accessible.name: "Actualizaciones"
                        }
                    }
                }

                SettingsPage {
                    id: downloadsSettingsPage
                    // The path under the name: it has the row's width, and the button keeps its column.
                    SettingRow {
                        label: "Carpeta"
                        detail: dialog.settings.nativeDownloadDirectory
                        unavailable: dialog.isolatedReason

                        Button {
                            id: changeFolderButton
                            text: "Cambiar…"
                            onClicked: folderDialog.open()
                            Accessible.name: "Cambiar la carpeta de descargas"
                            Accessible.onPressAction: changeFolderButton.click()
                            Keys.onReturnPressed: event => {
                                event.accepted = true
                                changeFolderButton.click()
                            }
                            Keys.onEnterPressed: event => {
                                event.accepted = true
                                changeFolderButton.click()
                            }
                        }
                    }

                    SettingRow {
                        label: "Límite de velocidad"

                        NativeSpeedLimit {
                            settings: dialog.settings
                            showApply: false
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

                    // As the web's preferences: which servers episodes come from. Compact 3-column
                    // grid keeps all Descargas rows within 1100x720 without scrolling.
                    SettingRow {
                        label: "Servidores"

                        GridLayout {
                            columns: 3
                            columnSpacing: 16
                            rowSpacing: 8

                            Repeater {
                                model: dialog.settings.providerSwitches

                                // A switch that cannot be used says why on hover and when the
                                // keyboard reaches it: a disabled control takes no focus itself.
                                Item {
                                    id: provider
                                    required property var modelData
                                    readonly property bool explained: modelData.reason !== ""
                                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
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
                                    ToolTip.visible: explained && (reasonHover.hovered
                                                                   || (activeFocus && InputModality.keyboard))
                                    ToolTip.delay: activeFocus && InputModality.keyboard
                                                   ? 0 : Theme.tooltipDelay
                                    ToolTip.timeout: Theme.tooltipTimeout
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
                                            visible: provider.activeFocus && InputModality.keyboard
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
                            model: ["No usar", "En este equipo", "Por MyJDownloader"]
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
                        label: "Dirección local"

                        Field {
                            text: dialog.settings.jdEndpoint
                            onEditingFinished: {
                                dialog.settings.jdEndpoint = text
                                text = dialog.settings.jdEndpoint
                            }
                            Accessible.name: "Dirección local de JDownloader"
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
                            AccentButton {
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
                            id: disconnectJdButton
                            visible: dialog.jd.account !== ""
                            text: "Desconectar"
                            onClicked: dialog.jd.disconnectAccount()
                            Accessible.name: "Desconectar MyJDownloader"
                            Accessible.onPressAction: disconnectJdButton.click()
                            Keys.onReturnPressed: event => {
                                event.accepted = true
                                disconnectJdButton.click()
                            }
                            Keys.onEnterPressed: event => {
                                event.accepted = true
                                disconnectJdButton.click()
                            }
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
                            id: pickJdButton
                            text: "Elegir…"
                            onClicked: jdFile.open()
                            Accessible.name: "Elegir la aplicación JDownloader"
                            Accessible.onPressAction: pickJdButton.click()
                            Keys.onReturnPressed: event => {
                                event.accepted = true
                                pickJdButton.click()
                            }
                            Keys.onEnterPressed: event => {
                                event.accepted = true
                                pickJdButton.click()
                            }
                        }
                        Button {
                            id: openJdButton
                            visible: dialog.settings.jdExecutable !== ""
                            text: "Abrir"
                            onClicked: dialog.jd.launch()
                            Accessible.name: "Abrir JDownloader"
                            Accessible.onPressAction: openJdButton.click()
                            Keys.onReturnPressed: event => {
                                event.accepted = true
                                openJdButton.click()
                            }
                            Keys.onEnterPressed: event => {
                                event.accepted = true
                                openJdButton.click()
                            }
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
                    id: aboutPage
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        // Identity card: app icon, version + platform, Copy diagnostics.
                        SettingsCard {
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 14

                                Image {
                                    Layout.preferredWidth: 44
                                    Layout.preferredHeight: 44
                                    source: "qrc:/qt/qml/AnimeHub/assets/animehub.png"
                                    mipmap: true
                                    Accessible.ignored: true
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: "AnimeHub Desktop"
                                        color: Theme.ink
                                        font.pixelSize: Theme.subtitle
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: "Versión " + dialog.app.version + " · " + dialog.app.platformLabel
                                        color: Theme.muted
                                        font.pixelSize: Theme.caption
                                        elide: Text.ElideRight
                                    }
                                }

                                Button {
                                    id: copyDiagnosticsButton
                                    text: dialog.diagnosticsCopied ? "Copiado" : "Copiar información"
                                    icon.source: Theme.source(dialog.diagnosticsCopied
                                                              ? Theme.icon.check : Theme.icon.copy)
                                    icon.width: 14
                                    icon.height: 14
                                    icon.color: dialog.diagnosticsCopied ? Theme.success : Theme.ink
                                    Accessible.name: "Copiar información técnica"
                                    Accessible.onPressAction: copyDiagnosticsButton.click()
                                    Keys.onReturnPressed: event => {
                                        event.accepted = true
                                        copyDiagnosticsButton.click()
                                    }
                                    Keys.onEnterPressed: event => {
                                        event.accepted = true
                                        copyDiagnosticsButton.click()
                                    }
                                    onClicked: {
                                        dialog.app.copyToClipboard(dialog.app.diagnosticsText())
                                        dialog.diagnosticsCopied = true
                                        copiedTimer.restart()
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 10
                                Layout.bottomMargin: 10
                                Layout.preferredHeight: 1
                                color: Qt.rgba(1, 1, 1, 0.07)
                            }

                            UpdateSection {
                                id: updateSection
                                updater: dialog.app.updater
                            }
                        }

                        // Links card: GitHub repository, release notes, issue tracker.
                        SettingsCard {
                            verticalPadding: 6

                            AboutLinkRow {
                                title: "Repositorio en GitHub"
                                subtitle: dialog.app.homepageUrl.toString().replace(/^https?:\/\//, "")
                                onClicked: dialog.app.openUrl(dialog.app.homepageUrl)
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                Layout.bottomMargin: 4
                                Layout.preferredHeight: 1
                                color: Qt.rgba(1, 1, 1, 0.07)
                            }

                            AboutLinkRow {
                                title: "Notas de la versión"
                                subtitle: "Cambios en la versión " + dialog.app.version
                                onClicked: dialog.app.openUrl(dialog.app.releasesUrl)
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                Layout.bottomMargin: 4
                                Layout.preferredHeight: 1
                                color: Qt.rgba(1, 1, 1, 0.07)
                            }

                            AboutLinkRow {
                                title: "Informar de un error"
                                subtitle: "Abrir incidencia en GitHub Issues"
                                onClicked: dialog.app.openUrl(dialog.app.issuesUrl)
                            }
                        }

                        // Diagnostics card: local data folder + Open button.
                        SettingsCard {
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Carpeta de datos"
                                        color: Theme.ink
                                        font.pixelSize: Theme.small
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: dialog.nativePath(dialog.app.dataDirectory)
                                        color: Theme.muted
                                        font.pixelSize: Theme.caption
                                        elide: Text.ElideMiddle
                                        HoverHandler { id: dataDirHover }
                                        ToolTip.visible: dataDirHover.hovered && truncated
                                        ToolTip.delay: Theme.tooltipDelay
                                        ToolTip.timeout: Theme.tooltipTimeout
                                        ToolTip.text: text
                                    }
                                }

                                Button {
                                    id: openDataDirButton
                                    text: "Abrir"
                                    icon.source: Theme.source(Theme.icon.folder)
                                    icon.width: 14
                                    icon.height: 14
                                    Accessible.name: "Abrir carpeta de datos"
                                    Accessible.onPressAction: openDataDirButton.click()
                                    Keys.onReturnPressed: event => {
                                        event.accepted = true
                                        openDataDirButton.click()
                                    }
                                    Keys.onEnterPressed: event => {
                                        event.accepted = true
                                        openDataDirButton.click()
                                    }
                                    onClicked: dialog.app.openDataDirectory()
                                }
                            }
                        }

                        // Collapsible SettingsExpander: engines and licenses (collapsed at rest so
                        // all four cards fit within 1100x720 with zero scroll).
                        SettingsCard {
                            id: enginesExpander
                            objectName: "aboutEnginesExpander"
                            property bool expanded: dialog.aboutEnginesExpanded
                            verticalPadding: 10

                            readonly property string summaryText: {
                                const parts = ["Qt " + dialog.app.qtVersion]
                                if (dialog.windows) {
                                    if (dialog.app.aria2Version !== "")
                                        parts.push("aria2 " + dialog.app.aria2Version)
                                    if (dialog.app.mpvVersion !== "")
                                        parts.push("mpv " + dialog.app.mpvVersion)
                                }
                                parts.push("SQLite")
                                parts.push("GNU GPL 3.0+")
                                return parts.join(" · ")
                            }

                            AbstractButton {
                                id: expanderHeader
                                Layout.fillWidth: true
                                implicitHeight: 38
                                padding: 0
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                activeFocusOnTab: true
                                Keys.onReturnPressed: event => {
                                    event.accepted = true
                                    expanderHeader.click()
                                }
                                Keys.onEnterPressed: event => {
                                    event.accepted = true
                                    expanderHeader.click()
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: "Motores y licencias"
                                Accessible.checkable: true
                                Accessible.checked: dialog.aboutEnginesExpanded
                                Accessible.onPressAction: click()
                                onClicked: dialog.aboutEnginesExpanded = !dialog.aboutEnginesExpanded

                                background: Rectangle {
                                    x: -8
                                    width: parent.width + 16
                                    radius: Theme.radiusSm
                                    color: expanderHeader.down ? Theme.pressSubtle
                                           : expanderHeader.hovered ? Theme.hoverSubtle : "transparent"

                                    FocusFrame {
                                        visible: expanderHeader.activeFocus && InputModality.keyboard
                                        radiusInside: Theme.radiusSm
                                    }
                                }

                                contentItem: RowLayout {
                                    spacing: 10

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            Layout.fillWidth: true
                                            text: "Motores y licencias"
                                            color: Theme.ink
                                            font.pixelSize: Theme.small
                                            font.weight: Font.DemiBold
                                            Accessible.ignored: true
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: enginesExpander.summaryText
                                            color: Theme.muted
                                            font.pixelSize: Theme.micro
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }
                                    }

                                    Glyph {
                                        glyph: dialog.aboutEnginesExpanded ? Theme.icon.chevronUp
                                                                           : Theme.icon.chevronDown
                                        size: 11
                                        color: expanderHeader.hovered ? Theme.ink : Theme.muted
                                    }
                                }
                            }

                            ColumnLayout {
                                visible: dialog.aboutEnginesExpanded
                                Layout.fillWidth: true
                                Layout.topMargin: 10
                                spacing: 10

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: Qt.rgba(1, 1, 1, 0.07)
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 20
                                    rowSpacing: 6

                                    Repeater {
                                        model: [
                                            "Qt Quick / FluentWinUI3", dialog.app.qtVersion,
                                            "aria2 (descargas)", dialog.windows && dialog.app.aria2Version !== ""
                                                                 ? dialog.app.aria2Version : "Del sistema",
                                            "mpv (reproducción)", dialog.windows && dialog.app.mpvVersion !== ""
                                                                  ? dialog.app.mpvVersion : "Del sistema",
                                            "SQLite", dialog.app.sqliteVersion,
                                            "Licencia", "GNU GPL 3.0 o posterior"
                                        ]

                                        delegate: Label {
                                            required property int index
                                            required property string modelData
                                            Layout.fillWidth: index % 2 === 1
                                            text: modelData
                                            color: index % 2 === 0 ? Theme.muted : Theme.ink
                                            font.pixelSize: Theme.caption
                                            font.weight: index % 2 === 1 ? Font.DemiBold : Font.Normal
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                RowLayout {
                                    visible: dialog.licenseFile.toString() !== ""
                                             || dialog.noticeFile.toString() !== ""
                                    Layout.topMargin: 2
                                    spacing: 8

                                    Button {
                                        id: licenseButton
                                        visible: dialog.licenseFile.toString() !== ""
                                        text: "Ver licencia"
                                        onClicked: dialog.app.openUrl(dialog.licenseFile)
                                        Accessible.name: "Ver la licencia"
                                        Accessible.onPressAction: licenseButton.click()
                                        Keys.onReturnPressed: event => {
                                            event.accepted = true
                                            licenseButton.click()
                                        }
                                        Keys.onEnterPressed: event => {
                                            event.accepted = true
                                            licenseButton.click()
                                        }
                                    }

                                    Button {
                                        id: thirdPartyButton
                                        visible: dialog.noticeFile.toString() !== ""
                                        text: "Terceros"
                                        onClicked: dialog.app.openUrl(dialog.noticeFile)
                                        Accessible.name: "Ver licencias de terceros"
                                        Accessible.onPressAction: thirdPartyButton.click()
                                        Keys.onReturnPressed: event => {
                                            event.accepted = true
                                            thirdPartyButton.click()
                                        }
                                        Keys.onEnterPressed: event => {
                                            event.accepted = true
                                            thirdPartyButton.click()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Declared after the pages: last in the Tab order.
    ToolButton {
        id: closeSettingsButton
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 8
        text: "Cerrar"
        display: AbstractButton.IconOnly
        icon.source: Theme.source(Theme.icon.cancel)
        ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.timeout: Theme.tooltipTimeout
        ToolTip.text: "Cerrar (Esc)"
        Accessible.name: "Cerrar ajustes"
        Accessible.onPressAction: closeSettingsButton.click()
        Keys.onReturnPressed: event => {
            event.accepted = true
            closeSettingsButton.click()
        }
        Keys.onEnterPressed: event => {
            event.accepted = true
            closeSettingsButton.click()
        }
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
