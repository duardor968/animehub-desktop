import QtQuick
import QtQuick.Controls
import AnimeHub

// What can be done with the chosen episodes; floats over the list, above the download bar.
// Where it does not fit, the marking actions move to a ⋯ menu, as in a WinUI CommandBar.
Rectangle {
    id: root

    required property AnimeView view
    // The widest the bar may grow.
    property real available: 10000

    signal cleared()

    readonly property int count: view.episodes.selectedCount
    readonly property bool overflow: clear.implicitWidth + label.implicitWidth
                                     + watched.implicitWidth + unwatched.implicitWidth
                                     + download.implicitWidth + 4 * row.spacing + 16 > available
    readonly property real maxLabelWidth: Math.max(80, available - clear.implicitWidth
                                                   - download.implicitWidth
                                                   - (overflow ? more.implicitWidth : 0)
                                                   - 3 * row.spacing - 20)

    width: Math.min(row.implicitWidth + 16, available)
    height: row.implicitHeight + 16
    radius: Theme.radiusLg
    color: Theme.raised
    border.color: Theme.stroke
    Accessible.role: Accessible.ToolBar
    Accessible.name: label.text

    // The bar floats over the rows: presses on its empty parts must not reach them.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = false
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4

        ToolButton {
            id: clear
            anchors.verticalCenter: parent.verticalCenter
            icon.source: Theme.source(Theme.icon.cancel)
            display: AbstractButton.IconOnly
            text: "Salir de la selección (Esc)"
            ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            onClicked: root.cleared()
        }

        Label {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            width: root.overflow ? Math.min(implicitWidth, root.maxLabelWidth) : implicitWidth
            rightPadding: 12
            text: root.count === 0 ? "Ningún episodio seleccionado"
                  : root.count === 1 ? "1 seleccionado" : root.count + " seleccionados"
            color: Theme.ink
            font.pixelSize: Theme.body
            elide: Text.ElideRight
        }

        Button {
            id: watched
            visible: !root.overflow
            anchors.verticalCenter: parent.verticalCenter
            flat: true
            enabled: root.count > 0
            text: "Marcar como visto"
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            onClicked: root.view.markSelection(true)
        }

        Button {
            id: unwatched
            visible: !root.overflow
            anchors.verticalCenter: parent.verticalCenter
            flat: true
            enabled: root.count > 0
            text: "Marcar como no visto"
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            onClicked: root.view.markSelection(false)
        }

        AccentButton {
            id: download
            anchors.verticalCenter: parent.verticalCenter
            emphasized: enabled
            enabled: root.view.episodes.selectedMissing > 0
            // What it queues, which the selection may not all be: "Descargar 5 episodios".
            text: enabled ? "Descargar " + root.view.episodes.selectedMissingText : "Descargar"
            glyphCode: Theme.icon.download
            onClicked: root.view.downloadSelection()
        }

        ToolButton {
            id: more
            visible: root.overflow
            enabled: root.count > 0
            anchors.verticalCenter: parent.verticalCenter
            icon.source: Theme.source(Theme.icon.more)
            display: AbstractButton.IconOnly
            text: "Más opciones"
            ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            onClicked: {
                overflowMenu.rebuild()
                overflowMenu.fit()
                overflowMenu.popup(more, more.width - overflowMenu.width + overflowMenu.rightInset,
                                   -overflowMenu.implicitHeight)
            }
        }
    }

    ActionMenu {
        id: overflowMenu

        entries: [markWatchedEntry, markUnwatchedEntry]

        MenuEntry {
            id: markWatchedEntry
            property bool wanted: root.count > 0
            text: "Marcar como visto"
            glyph: Theme.icon.check
            onTriggered: root.view.markSelection(true)
        }
        MenuEntry {
            id: markUnwatchedEntry
            property bool wanted: root.count > 0
            text: "Marcar como no visto"
            glyph: Theme.icon.undo
            onTriggered: root.view.markSelection(false)
        }
    }
}
