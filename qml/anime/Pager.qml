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
        icon.source: Theme.source("e76b")
        display: AbstractButton.IconOnly
        text: "Episodios anteriores"
        ToolTip.visible: hovered
        ToolTip.text: text
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
        icon.source: Theme.source("e76c")
        display: AbstractButton.IconOnly
        text: "Episodios siguientes"
        ToolTip.visible: hovered
        ToolTip.text: text
        onClicked: root.go(root.episodes.page + 1)
    }
}
