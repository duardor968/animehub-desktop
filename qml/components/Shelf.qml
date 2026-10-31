import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A titled row of whole posters; the arrows page through the rest. Items declared inside it
// (a button acting on the whole row) go beside the title. For the keyboard the row is one stop:
// ← and → walk its posters and page with them, Enter opens one.
Item {
    id: root

    property alias model: list.model
    property string title
    // Shown after the title, muted; below 0, nothing.
    property int count: -1
    default property alias actions: actionsRow.data

    signal activated(string slug, real episode)
    // A poster's own button: play the episode or download what it lists.
    signal acted(string slug, real episode, string action)

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

    // The posters 12 px under the title; the frame's margin around them takes no room.
    implicitHeight: heading.height + 12 + list.height
    visible: list.count > 0

    RowLayout {
        id: heading
        width: root.width
        spacing: 8

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

        // A nested layout would fill the row by default and push the hairline away.
        RowLayout {
            id: actionsRow
            Layout.fillWidth: false
            Layout.leftMargin: 12
            spacing: 8
        }

        // As Steam's shelves: a hairline to the edge, or to the arrows.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.preferredHeight: 1
            color: Theme.stroke
        }

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

    // Clips the posters of other pages, a little outside the row: the keyboard's frame stays whole
    // and the gap keeps the neighbours out.
    Item {
        x: -4
        y: heading.height + 12 - 4
        width: Math.max(0, root.width) + 8
        height: list.height + 8
        clip: true

        ListView {
            id: list
            x: 4
            y: 4
            width: Math.max(0, root.width)
            // Fixed so the row exists before its delegates; nothing while the row has no width.
            height: root.cardWidth > 0 ? Theme.posterCardHeight(root.cardWidth) : 0
            orientation: ListView.Horizontal
            spacing: root.gap
            interactive: false
            highlightRangeMode: ListView.NoHighlightRange
            // The row pages itself; it never stops between two posters.
            highlightFollowsCurrentItem: false
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Accessible.name: root.title
            Behavior on contentX {
                enabled: root.paging
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }

            // The poster with the keyboard stays on view: the row pages to it.
            onCurrentIndexChanged: {
                if (currentIndex < 0)
                    return
                if (currentIndex < root.first)
                    root.show(currentIndex, true)
                else if (currentIndex >= root.first + root.columns)
                    root.show(currentIndex - root.columns + 1, true)
            }
            // A row reached by keyboard starts at the first poster on view.
            onActiveFocusChanged: {
                if (activeFocus && (currentIndex < root.first || currentIndex >= root.first + root.columns))
                    currentIndex = root.first
            }

            // New posters start the row again.
            Connections {
                target: list.model
                function onModelReset() { root.first = 0 }
            }

            delegate: PosterCard {
                required property var model
                required property int index

                width: root.cardWidth
                text: model.title
                posterUrl: model.posterUrl
                caption: model.caption
                progress: model.progress
                fresh: model.fresh
                quickAction: model.action ?? ""
                owned: model.owned ?? false
                // The row is the keyboard's stop, not each poster.
                focusPolicy: Qt.ClickFocus
                onClicked: {
                    list.currentIndex = index
                    root.activated(model.slug, model.episode)
                }
                onAct: root.acted(model.slug, model.episode, model.action)
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
}
