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
                           ? Qt.rgba(0.30, 0.72, 0.51, 0.16)
                           : section.updater.error
                             ? Qt.rgba(0.90, 0.33, 0.29, 0.16)
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
                    font.pixelSize: Theme.body - 1
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
                    font.pixelSize: Theme.caption - 1
                    elide: Text.ElideRight

                    HoverHandler { id: subtitleHover }
                    ToolTip.visible: subtitleHover.hovered && subtitleLabel.truncated
                    ToolTip.text: subtitleLabel.text
                }
            }
        }

        RowLayout {
            Layout.alignment: section.compact ? (Qt.AlignRight | Qt.AlignVCenter) : Qt.AlignVCenter
            Layout.fillWidth: false
            spacing: 8

            Button {
                visible: section.updater.state === "uptodate" || section.updater.checking
                enabled: !section.updater.checking
                text: "Buscar actualizaciones"
                Accessible.name: "Buscar actualizaciones"
                onClicked: section.updater.checkNow()
            }

            Button {
                visible: section.updater.downloading
                enabled: section.updater.state === "downloading"
                text: "Cancelar"
                Accessible.name: "Cancelar descarga de actualización"
                onClicked: section.updater.cancelDownload()
            }

            Button {
                visible: section.canExpandNotes
                flat: true
                text: "Novedades"
                icon.source: Theme.source(section.notesExpanded
                                          ? Theme.icon.collapseAll : Theme.icon.expandAll)
                icon.width: 10
                icon.height: 10
                LayoutMirroring.enabled: true
                Accessible.name: "Novedades de la versión " + section.updater.targetVersion
                Accessible.checkable: true
                Accessible.checked: section.notesExpanded
                onClicked: section.notesExpanded = !section.notesExpanded
            }

            AccentButton {
                visible: section.updater.available && !section.updater.manualOnly
                text: "Descargar"
                glyphCode: Theme.icon.download
                fontPixelSize: Theme.body - 1
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                Accessible.onPressAction: click()
                onClicked: section.updater.downloadNow()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    click()
                }
            }

            AccentButton {
                visible: section.updater.available && section.updater.manualOnly
                text: "Descargar manualmente"
                glyphCode: Theme.icon.openExternal
                fontPixelSize: Theme.body - 1
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                Accessible.onPressAction: click()
                onClicked: section.updater.openManualDownload()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    click()
                }
            }

            AccentButton {
                visible: section.updater.ready
                enabled: !section.updater.busy
                text: "Actualizar y reiniciar"
                glyphCode: Theme.icon.update
                fontPixelSize: Theme.body - 1
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                Accessible.onPressAction: click()
                onClicked: section.updater.requestApply()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    click()
                }
            }

            Button {
                visible: section.updater.error
                flat: true
                text: "Descargar manualmente"
                Accessible.name: "Descargar actualización manualmente"
                onClicked: section.updater.openManualDownload()
            }

            AccentButton {
                visible: section.updater.error
                text: "Reintentar"
                glyphCode: Theme.icon.retry
                fontPixelSize: Theme.body - 1
                implicitHeight: 32
                leftPadding: 13
                rightPadding: 13
                Accessible.onPressAction: click()
                onClicked: section.updater.checkNow()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    click()
                }
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
        radius: 5
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
