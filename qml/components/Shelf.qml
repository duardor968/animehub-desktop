import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A titled row of whole posters; the arrows page through the rest. Items declared inside it
// (a button acting on the whole row) go beside the title.
Column {
    id: root

    property alias model: list.model
    property string title
    // Shown after the title, muted; below 0, nothing.
    property int count: -1
    default property alias actions: actionsRow.data

    signal activated(string slug, real episode)

    readonly property int gap: Theme.posterGap
    readonly property int columns: Theme.posterColumns(width)
    readonly property real cardWidth: (width - (columns - 1) * gap) / columns
    // The first whole poster on view; kept across resizes so the row never stops mid-poster.
    property int first: 0
    property bool paging: false

    function show(index, animated) {
        first = Math.max(0, Math.min(index, list.count - columns))
        paging = animated
        list.positionViewAtIndex(first, ListView.Beginning)
        paging = false
    }
    function page(step) { show(first + step * columns, true) }

    onCardWidthChanged: show(first, false)

    spacing: 12
    visible: list.count > 0

    RowLayout {
        width: root.width

        Label {
            text: root.title
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }

        Label {
            visible: root.count >= 0
            text: "(" + root.count + ")"
            color: Theme.muted
            font.pixelSize: Theme.subtitle
        }

        RowLayout {
            id: actionsRow
            Layout.leftMargin: 12
            spacing: 8
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            visible: list.count > root.columns
            enabled: root.first > 0
            icon.source: Theme.source("e76b")
            display: AbstractButton.IconOnly
            text: "Anteriores"
            ToolTip.visible: hovered
            ToolTip.text: text
            onClicked: root.page(-1)
        }

        ToolButton {
            visible: list.count > root.columns
            enabled: root.first + root.columns < list.count
            icon.source: Theme.source("e76c")
            display: AbstractButton.IconOnly
            text: "Siguientes"
            ToolTip.visible: hovered
            ToolTip.text: text
            onClicked: root.page(1)
        }
    }

    ListView {
        id: list
        width: root.width
        // Fixed so the row exists before its delegates.
        height: Theme.posterCardHeight(root.cardWidth)
        orientation: ListView.Horizontal
        spacing: root.gap
        clip: true
        interactive: false
        highlightRangeMode: ListView.NoHighlightRange
        keyNavigationEnabled: true
        Accessible.name: root.title
        Behavior on contentX {
            enabled: root.paging
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }

        // New posters start the row again.
        Connections {
            target: list.model
            function onModelReset() { root.first = 0 }
        }

        delegate: PosterCard {
            required property var model

            width: root.cardWidth
            text: model.title
            posterUrl: model.posterUrl
            caption: model.caption
            progress: model.progress
            fresh: model.fresh
            onClicked: root.activated(model.slug, model.episode)
        }

        // Wheel and touchpad scroll sideways, a page at a time.
        WheelHandler {
            // Sideways gestures only: vertical wheel keeps scrolling the page.
            orientation: Qt.Horizontal
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => {
                const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                if (delta === 0 || list.count <= root.columns)
                    return
                root.page(delta < 0 ? 1 : -1)
            }
        }
    }
}
