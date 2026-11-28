import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Pages of fifty episodes, as on the web: the span in a drop-down, with ‹ › beside it.
RowLayout {
    id: root

    required property EpisodeModel episodes

    // The user moved to another page.
    signal moved()

    function go(page) {
        episodes.page = page
        moved()
    }

    spacing: 4

    ToolButton {
        enabled: root.episodes.page > 0
        icon.source: Theme.source(Theme.icon.chevronLeft)
        display: AbstractButton.IconOnly
        text: "Episodios anteriores"
        ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.timeout: Theme.tooltipTimeout
        ToolTip.text: text
        Accessible.onPressAction: click()
        Keys.onReturnPressed: event => { event.accepted = true; click() }
        Keys.onEnterPressed: event => { event.accepted = true; click() }
        onClicked: root.go(root.episodes.page - 1)
    }

    ComboBox {
        id: span
        model: root.episodes.pageLabels
        currentIndex: root.episodes.page
        implicitContentWidthPolicy: ComboBox.WidestText
        Accessible.name: "Episodios " + displayText
        onActivated: index => root.go(index)

        // The arrows move the page too: keep the box on it.
        Connections {
            target: root.episodes
            function onPageChanged() { span.currentIndex = root.episodes.page }
        }
    }

    ToolButton {
        enabled: root.episodes.page < root.episodes.pageCount - 1
        icon.source: Theme.source(Theme.icon.chevronRight)
        display: AbstractButton.IconOnly
        text: "Episodios siguientes"
        ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.timeout: Theme.tooltipTimeout
        ToolTip.text: text
        Accessible.onPressAction: click()
        Keys.onReturnPressed: event => { event.accepted = true; click() }
        Keys.onEnterPressed: event => { event.accepted = true; click() }
        onClicked: root.go(root.episodes.page + 1)
    }
}
