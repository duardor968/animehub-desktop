import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Unified updater block integrated inside Card 1 of Ajustes -> Acerca de.
// Uses the same crisp layout for patch, minor, and major updates, with release notes
// always accessible via "Novedades" whenever notes are non-empty.
ColumnLayout {
    id: section
    objectName: "updateSection"

    required property UpdateView updater
    property bool notesExpanded: false

    readonly property bool compact: width > 0 && width < 480
    readonly property bool canExpandNotes: (updater.available || updater.ready) && updater.notes !== ""

    Layout.fillWidth: true
    spacing: 10

    Connections {
        target: section.updater
        function onChanged() {
            if (!section.canExpandNotes)
                section.notesExpanded = false
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: section.compact ? 1 : 2
        columnSpacing: 12
        rowSpacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 14

            // Match Row 1's 44 px app-icon slot so icon centers and text columns align to 0 px.
            Item {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    radius: 14
                    color: section.updater.state === "uptodate"
                           ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.16)
                           : section.updater.error
                             ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.16)
                             : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)

                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        visible: section.updater.checking
                        running: visible
                        Accessible.ignored: true
                    }

                    Glyph {
                        anchors.centerIn: parent
                        visible: !section.updater.checking
                        size: 13
                        glyph: section.updater.state === "uptodate"
                               ? Theme.icon.check
                               : section.updater.error
                                 ? Theme.icon.warning
                                 : section.updater.downloading
                                   ? Theme.icon.download
                                   : Theme.icon.update
                        color: section.updater.state === "uptodate"
                               ? Theme.success
                               : section.updater.error
                                 ? Theme.danger
                                 : Theme.accentText
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: {
                        switch (section.updater.state) {
                        case "checking":    return "Buscando actualizaciones…"
                        case "available":   return "Nueva versión " + section.updater.targetVersion
                        case "downloading": return "Descargando " + section.updater.targetVersion
                        case "verifying":   return "Verificando " + section.updater.targetVersion
                        case "ready":       return "Versión " + section.updater.targetVersion + " lista"
                        case "applying":    return "Aplicando actualización…"
                        case "error":       return "No se pudo actualizar"
                        default:            return "Estás al día"
                        }
                    }
                    color: Theme.ink
                    font.pixelSize: Theme.small
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    id: subtitleLabel
                    Layout.fillWidth: true
                    text: {
                        switch (section.updater.state) {
                        case "checking":
                            return "Comprobando si hay una versión nueva…"
                        case "available":
                            if (section.updater.manualOnly) {
                                return section.updater.sizeText !== ""
                                       ? "Actualización manual · " + section.updater.sizeText
                                       : "Actualización manual disponible"
                            }
                            return section.updater.sizeText
                        case "downloading":
                        case "verifying":
                            return section.updater.progressDetailText
                        case "ready":
                            return section.updater.readySubtitleText
                        case "applying":
                            return "Reiniciando AnimeHub…"
                        case "error":
                            return section.updater.errorDetailText
                        default:
                            return section.updater.lastCheckedText
                        }
                    }
                    visible: text !== ""
                    color: section.updater.error ? Theme.danger : Theme.muted
                    font.pixelSize: Theme.micro
                    elide: Text.ElideRight

                    HoverHandler { id: subtitleHover }
                    ToolTip.visible: subtitleHover.hovered && subtitleLabel.truncated
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: subtitleLabel.text
                }
            }
        }

        RowLayout {
            Layout.alignment: section.compact ? (Qt.AlignRight | Qt.AlignVCenter) : Qt.AlignVCenter
            Layout.fillWidth: false
            spacing: 8

            Button {
                id: checkNowButton
                visible: section.updater.state === "uptodate" || section.updater.checking
                enabled: !section.updater.checking
                text: "Buscar actualizaciones"
                Accessible.name: "Buscar actualizaciones"
                Accessible.onPressAction: checkNowButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    checkNowButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    checkNowButton.click()
                }
                onClicked: section.updater.checkNow()
            }

            Button {
                id: cancelDownloadButton
                visible: section.updater.downloading
                enabled: section.updater.state === "downloading"
                text: "Cancelar"
                Accessible.name: "Cancelar descarga de actualización"
                Accessible.onPressAction: cancelDownloadButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    cancelDownloadButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    cancelDownloadButton.click()
                }
                onClicked: section.updater.cancelDownload()
            }

            Button {
                id: notesButton
                visible: section.canExpandNotes
                flat: true
                text: "Novedades"
                Accessible.name: "Novedades de la versión " + section.updater.targetVersion
                Accessible.checkable: true
                Accessible.checked: section.notesExpanded
                Accessible.onPressAction: notesButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    notesButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    notesButton.click()
                }
                onClicked: section.notesExpanded = !section.notesExpanded

                contentItem: RowLayout {
                    spacing: 6

                    Label {
                        text: notesButton.text
                        color: notesButton.enabled ? Theme.ink : Theme.muted
                        font: notesButton.font
                        Accessible.ignored: true
                    }

                    Glyph {
                        glyph: section.notesExpanded ? Theme.icon.chevronUp : Theme.icon.chevronDown
                        size: 10
                        color: notesButton.hovered ? Theme.ink : Theme.muted
                    }
                }
            }

            AccentButton {
                visible: section.updater.available && !section.updater.manualOnly
                text: "Descargar"
                glyphCode: Theme.icon.download
                fontPixelSize: Theme.small
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                onClicked: section.updater.downloadNow()
            }

            AccentButton {
                visible: section.updater.available && section.updater.manualOnly
                text: "Descargar manualmente"
                glyphCode: Theme.icon.openExternal
                fontPixelSize: Theme.small
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                onClicked: section.updater.openManualDownload()
            }

            AccentButton {
                visible: section.updater.ready
                enabled: !section.updater.busy
                text: "Actualizar y reiniciar"
                glyphCode: Theme.icon.update
                fontPixelSize: Theme.small
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                onClicked: section.updater.requestApply()
            }

            Button {
                id: manualErrorButton
                visible: section.updater.error
                flat: true
                text: "Descargar manualmente"
                Accessible.name: "Descargar actualización manualmente"
                Accessible.onPressAction: manualErrorButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    manualErrorButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    manualErrorButton.click()
                }
                onClicked: section.updater.openManualDownload()
            }

            AccentButton {
                visible: section.updater.error
                text: "Reintentar"
                glyphCode: Theme.icon.retry
                fontPixelSize: Theme.small
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                onClicked: section.updater.checkNow()
            }
        }
    }

    ProgressLine {
        visible: section.updater.downloading
        Layout.fillWidth: true
        value: section.updater.progress
    }

    Rectangle {
        visible: section.notesExpanded && section.canExpandNotes
        Layout.fillWidth: true
        implicitHeight: notesCol.implicitHeight + 20
        radius: Theme.radius
        color: Qt.rgba(0, 0, 0, 0.22)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.07)

        ColumnLayout {
            id: notesCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 5

            Label {
                Layout.fillWidth: true
                text: "Cambios en AnimeHub " + section.updater.targetVersion
                color: Theme.ink
                font.pixelSize: Theme.caption
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: Theme.caption
                lineHeight: 1.35
                text: section.updater.notes
            }
        }
    }
}
