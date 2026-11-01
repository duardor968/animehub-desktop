import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AnimeHub

// JDownloader for the servers AnimeHub does not download: installing it (only when this PC
// has none), how it is reached, and a test that turns it on and sends the waiting episodes to
// it. JDownloader's own settings are only described, never changed.
ModalDialog {
    id: dialog
    objectName: "jdSetup"

    required property AppController app
    readonly property JDownloaderView jd: app.jdownloader
    readonly property bool windows: Qt.platform.os === "windows"

    property int step: 0
    // The first step shown: installing is left out when JDownloader is already here.
    property int first: 0
    property bool local: true
    readonly property bool found: jd.executable !== ""
    // Signing in to MyJDownloader is part of the test.
    readonly property bool signingIn: !local && jd.account === ""
    readonly property var names: ["Instalar JDownloader", "Conectar", "Probar la conexión"]
    readonly property bool canGoOn: step !== 1 || local || !signingIn
                                    || (email.text.trim() !== "" && password.text !== "")

    // Opens on step `at` (0 to 2); the last one tests at once.
    function begin(at, viaAccount) {
        jd.findExecutable()
        first = found ? 1 : 0
        local = !viaAccount && found
        localRadio.checked = local
        accountRadio.checked = !local
        password.text = ""
        step = Math.max(first, at ?? 0)
        open()
        if (step === 2)
            test()
    }
    function test() {
        if (signingIn)
            jd.testAccount(email.text, password.text)
        else
            jd.testConnection(local)
    }
    // The footer's main button: on to the next step, or the test again.
    function forward() {
        if (!canGoOn || jd.testing)
            return
        if (step < 2)
            step += 1
        if (step === 2)
            test()
    }

    readonly property int columnWidth: 472
    width: 520
    title: "Usar JDownloader"
    onOpened: primary.forceActiveFocus()
    // The main button is off while the test runs; the keyboard goes back to it after.
    readonly property bool testing: jd.testing
    onTestingChanged: if (!testing && visible) primary.forceActiveFocus()
    onClosed: password.text = ""
    // Installed meanwhile (from the browser): on to how it is reached.
    onFoundChanged: if (visible && step === 0 && found) step = 1

    Connections {
        target: dialog.jd
        function onConnectionReady() { dialog.close() }
    }
    // Back from installing it in the browser: it is looked for again.
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (dialog.visible && dialog.step === 0 && Qt.application.state === Qt.ApplicationActive)
                dialog.jd.findExecutable()
        }
    }

    // One title, and the step under it.
    header: ColumnLayout {
        spacing: 4

        Label {
            Layout.topMargin: dialog.padding
            Layout.leftMargin: dialog.padding
            Layout.rightMargin: dialog.padding
            text: dialog.title
            color: Theme.ink
            font.pixelSize: 20
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }
        Label {
            Layout.leftMargin: dialog.padding
            Layout.rightMargin: dialog.padding
            text: "Paso " + (dialog.step - dialog.first + 1) + " de " + (3 - dialog.first) + " · "
                  + dialog.names[dialog.step]
            color: Theme.muted
            font.pixelSize: Theme.body
        }
    }

    component Line: Label {
        Layout.preferredWidth: dialog.columnWidth
        color: Theme.ink
        wrapMode: Text.Wrap
    }
    component Note: Label {
        Layout.preferredWidth: dialog.columnWidth
        color: Theme.muted
        wrapMode: Text.Wrap
    }
    component Field: TextField {
        Layout.preferredWidth: 320
        onAccepted: dialog.forward()
    }
    // Enter acts on a button inside the steps, not on the footer's main one.
    component StepButton: Button {
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
    }

    // Fixed widths: Fluent's Dialog loops when its content follows it. Only the step on view
    // takes room: the dialog is as tall as it.
    ColumnLayout {
        width: dialog.columnWidth
        spacing: 0
        // Enter anywhere else is the footer's main button.
        Keys.onReturnPressed: dialog.forward()
        Keys.onEnterPressed: dialog.forward()

        // Installing it; shown only when this PC has none.
        ColumnLayout {
            visible: dialog.step === 0
            spacing: 12

            Line { text: "JDownloader no está instalado en este PC." }
            RowLayout {
                spacing: 8

                StepButton {
                    text: "Descargar JDownloader"
                    icon.source: Theme.source(Theme.icon.download)
                    onClicked: dialog.jd.openDownloadPage()
                    Accessible.name: "Descargar JDownloader desde jdownloader.org"
                }
                StepButton {
                    text: "Elegir…"
                    onClicked: executableFile.open()
                    Accessible.name: "Elegir la aplicación JDownloader"
                }
            }
            Note { text: "Si está en otro equipo, continúa." }
        }

        // How it is reached.
        ColumnLayout {
            visible: dialog.step === 1
            spacing: 12

            ButtonGroup { id: reach }
            RadioButton {
                id: localRadio
                text: "En este PC"
                enabled: dialog.found
                ButtonGroup.group: reach
                onToggled: if (checked) dialog.local = true
            }

            // Its local API is off by default: the steps to turn it on, under its choice.
            ColumnLayout {
                visible: dialog.local
                Layout.leftMargin: 28
                spacing: 8

                RowLayout {
                    spacing: 12
                    Line {
                        Layout.preferredWidth: dialog.columnWidth - 28 - 12 - openJd.implicitWidth
                        text: "1. Abre JDownloader."
                    }
                    StepButton {
                        id: openJd
                        text: "Abrir JDownloader"
                        onClicked: dialog.jd.launch()
                    }
                }
                Line {
                    Layout.preferredWidth: dialog.columnWidth - 28
                    text: "2. En Ajustes › Configuración avanzada, busca <b>RemoteAPI: Deprecated "
                          + "Api Enabled</b> y actívalo."
                    textFormat: Text.StyledText
                }
                Line {
                    Layout.preferredWidth: dialog.columnWidth - 28
                    text: "3. Reinicia JDownloader."
                }
            }

            RadioButton {
                id: accountRadio
                text: "Por MyJDownloader"
                ButtonGroup.group: reach
                onToggled: if (checked) dialog.local = false
            }

            // Its account, as in Ajustes; "Siguiente" signs in.
            ColumnLayout {
                visible: !dialog.local
                Layout.leftMargin: 28
                spacing: 8

                Field {
                    id: email
                    visible: dialog.signingIn
                    placeholderText: "Correo"
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                    Accessible.name: "Correo de MyJDownloader"
                }
                Field {
                    id: password
                    visible: dialog.signingIn
                    placeholderText: "Contraseña"
                    echoMode: TextInput.Password
                    Accessible.name: "Contraseña de MyJDownloader"
                }
                Line {
                    visible: !dialog.signingIn
                    Layout.preferredWidth: dialog.columnWidth - 28
                    text: dialog.jd.account
                }
                ComboBox {
                    visible: !dialog.signingIn && dialog.jd.devices.length > 1
                    Layout.preferredWidth: 320
                    textRole: "name"
                    valueRole: "id"
                    model: dialog.jd.devices
                    currentIndex: Math.max(0, indexOfValue(dialog.app.settings.jdownloaderDevice))
                    onActivated: dialog.app.settings.jdownloaderDevice = currentValue
                    Accessible.name: "Equipo con JDownloader"
                }
            }
        }

        // The test: while it runs a moving bar, then why it failed.
        ColumnLayout {
            visible: dialog.step === 2
            spacing: 12

            Line {
                visible: dialog.jd.testing
                text: dialog.signingIn ? "Conectando con MyJDownloader…" : "Conectando con JDownloader…"
            }
            ProgressBar {
                visible: dialog.jd.testing
                Layout.preferredWidth: dialog.columnWidth
                indeterminate: true
                Accessible.name: "Conectando"
            }
            Label {
                Layout.preferredWidth: dialog.columnWidth
                visible: !dialog.jd.testing && text !== ""
                text: dialog.jd.testProblem
                color: Theme.danger
                wrapMode: Text.Wrap
            }
        }
    }

    // Windows' wizard order: Atrás, the main action, Cancelar.
    footer: DialogButtonBox {
        Button {
            visible: dialog.step > dialog.first
            text: "Atrás"
            enabled: !dialog.jd.testing
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.step -= 1
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            id: primary
            text: dialog.step < 2 ? "Siguiente" : dialog.jd.testProblem !== "" ? "Probar otra vez" : "Probar"
            enabled: dialog.canGoOn && !dialog.jd.testing
            highlighted: dialog.visible // DialogButtonBox clears it while it builds
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.forward()
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Keys.onReturnPressed: dialog.reject()
            Keys.onEnterPressed: dialog.reject()
        }
    }

    FileDialog {
        id: executableFile
        title: "Elegir JDownloader"
        // On Linux JDownloader2 is a script without an extension.
        nameFilters: dialog.windows ? ["Aplicaciones (*.exe)"] : []
        onAccepted: dialog.app.settings.jdExecutable = dialog.app.localPath(selectedFile)
    }
}
