import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Compact speed limit control shared by Settings (inline row, applies immediately) and the
// Downloads header popover (header + switch over value, unit and "Aplicar").
Item {
    id: editor

    required property AppSettings settings
    // True inside the Downloads popover; false in Settings where every edit applies at once.
    property bool showApply: false

    signal applied()

    property bool syncing: false

    function focusToggle(reason) {
        toggle.forceActiveFocus(reason !== undefined ? reason : Qt.TabFocusReason)
    }

    function resetFromSettings() {
        toggle.checked = settings.nativeLimitEnabled
        syncFromSettings(true)
    }

    function formatValue(kbps, inMB) {
        if (!inMB)
            return String(kbps)
        if (kbps % 1000 === 0)
            return String(kbps / 1000)
        return String(Math.round((kbps / 1000) * 100) / 100).replace(".", ",")
    }

    function syncFromSettings(force) {
        if (!force && valueField.activeFocus)
            return
        syncing = true
        const kbps = settings.nativeLimitKBps
        const inMB = kbps >= 1000 && kbps % 10 === 0
        unitBox.currentIndex = inMB ? 0 : 1
        valueField.text = formatValue(kbps, inMB)
        syncing = false
    }

    function parseKBps() {
        const raw = valueField.text.trim().replace(",", ".")
        if (raw === "")
            return -1
        const num = Number(raw)
        if (!Number.isFinite(num) || num <= 0)
            return -1
        const kbps = unitBox.currentIndex === 0 ? Math.round(num * 1000) : Math.round(num)
        return Math.max(1, Math.min(1000000, kbps))
    }

    function commit() {
        if (syncing)
            return
        const kbps = parseKBps()
        if (kbps < 1) {
            syncFromSettings(true)
            return
        }
        settings.nativeLimitKBps = kbps
        syncFromSettings(true)
    }

    implicitWidth: showApply ? Math.max(272, grid.implicitWidth) : grid.implicitWidth
    implicitHeight: grid.implicitHeight

    Component.onCompleted: resetFromSettings()

    Connections {
        target: editor.settings
        function onSpeedLimitChanged() {
            toggle.checked = editor.settings.nativeLimitEnabled
            editor.syncFromSettings(false)
        }
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        columns: 3
        columnSpacing: 8
        rowSpacing: editor.showApply ? 12 : 0

        Label {
            id: headerLabel
            visible: editor.showApply
            Layout.row: editor.showApply ? 0 : 1
            Layout.column: 0
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: "Límite de velocidad"
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
        }

        Switch {
            id: toggle
            Layout.row: 0
            Layout.column: editor.showApply ? 2 : 0
            Layout.alignment: (editor.showApply ? Qt.AlignRight : Qt.AlignLeft) | Qt.AlignVCenter
            Layout.preferredWidth: implicitIndicatorWidth
            Accessible.name: "Limitar velocidad de descarga"
            onToggled: editor.settings.nativeLimitEnabled = checked
        }

        TextField {
            id: valueField
            Layout.row: editor.showApply ? 1 : 0
            Layout.column: editor.showApply ? 0 : 1
            Layout.fillWidth: editor.showApply
            Layout.minimumWidth: editor.showApply ? 72 : 88
            Layout.preferredWidth: 88
            Layout.alignment: Qt.AlignVCenter
            enabled: toggle.checked
            horizontalAlignment: Text.AlignRight
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: RegularExpressionValidator {
                regularExpression: /[0-9]*([.,][0-9]{0,2})?/
            }
            Accessible.name: "Límite de velocidad"
            onEditingFinished: {
                if (!editor.showApply)
                    editor.commit()
            }
            onAccepted: {
                editor.commit()
                if (editor.showApply)
                    editor.applied()
            }
        }

        ComboBox {
            id: unitBox
            Layout.row: editor.showApply ? 1 : 0
            Layout.column: editor.showApply ? 1 : 2
            Layout.preferredWidth: 86
            Layout.alignment: Qt.AlignVCenter
            enabled: toggle.checked
            model: ["MB/s", "KB/s"]
            Accessible.name: "Unidad de límite de velocidad"
            onActivated: {
                if (!editor.showApply)
                    editor.commit()
            }
        }

        AccentButton {
            id: applyButton
            visible: editor.showApply
            Layout.row: 1
            Layout.column: 2
            Layout.alignment: Qt.AlignVCenter
            text: "Aplicar"
            emphasized: toggle.checked
            enabled: toggle.checked
            Accessible.name: "Aplicar límite de velocidad"
            onClicked: {
                editor.commit()
                editor.applied()
            }
        }
    }
}
