import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The band under a page's header, as Steam's under a game: the main action on the left with the
// page's own buttons beside it (favourite, alerts, more), then the figures. The page scrolls it
// with the header until it reaches the top, where it stays, led there by the poster and title it
// stands for.
Rectangle {
    id: root

    // [{caption, value, progress (< 0: no bar), problem, action}]. Beside the actions while they
    // all fit there; otherwise on a line of their own under them, wrapping as they need: none is
    // ever left out. With `action`, the name of what its button next to the value does
    // (figureAction).
    property var figures: []
    // Stuck to the top: a hairline parts it from what scrolls under it.
    property bool pinned: false
    // What the page is about, shown once the header that says it has scrolled away.
    property string title
    property url posterUrl
    // The band spans the page; its content keeps to the page's column from the left margin.
    property real contentWidth: width - 2 * Theme.section
    // The main action and what goes with it.
    default property alias actions: leading.data
    // The page's own buttons, next to the main action.
    property alias trailing: trailingRow.data
    // The page's title. The bar floats over the page, after its content in the item tree: Tab
    // goes from the title into the bar (`enter`) and from the bar on to what follows the title;
    // Mayús+Tab from the bar goes back to the title.
    property Item titleItem: null

    signal figureAction(var figure)

    function enter() {
        const first = head.nextItemInFocusChain(true)
        if (!root.visible || first === tail)
            return false
        first.forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    // The figures under the actions: they do not fit beside them.
    readonly property bool stacked: figures.length > 0 && figureFlow.needed > figureArea.width

    implicitHeight: stacked ? 72 + figureFlow.height + 16 : 72
    color: Theme.pane
    Accessible.role: Accessible.ToolBar

    // A caption over its value, as Steam lays out its figures; a bar under it for a share.
    component Figure: ColumnLayout {
        id: figure

        required property var modelData

        spacing: 3
        Accessible.role: Accessible.StaticText
        Accessible.name: modelData.caption + ": " + modelData.value

        Label {
            text: figure.modelData.caption
            color: Theme.muted
            font.pixelSize: Theme.caption
        }

        RowLayout {
            spacing: 4

            Label {
                id: valueLabel
                text: figure.modelData.value
                color: figure.modelData.problem ? Theme.danger : Theme.ink
                font.pixelSize: Theme.body
                font.weight: Font.DemiBold
            }

            // Out of the column's height: the value's line stays where every figure's does.
            Item {
                visible: (figure.modelData.action ?? "") !== ""
                implicitWidth: figureButton.implicitWidth
                implicitHeight: valueLabel.implicitHeight

                ToolButton {
                    id: figureButton
                    anchors.verticalCenter: parent.verticalCenter
                    icon.source: Theme.source(Theme.icon.retry)
                    display: AbstractButton.IconOnly
                    text: figure.modelData.action ?? ""
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: root.figureAction(figure.modelData)
                }
            }
        }

        ProgressLine {
            Layout.fillWidth: true
            Layout.topMargin: 3
            // Wide enough to read as a share next to a short value like "12/13".
            implicitWidth: 96
            visible: figure.modelData.progress >= 0
            value: figure.modelData.progress
        }
    }

    // The bar's ends in the Tab order, which hand the keyboard on: before its first control, back
    // to the title; after its last one, on to what follows the title.
    Item {
        id: head
        activeFocusOnTab: root.titleItem !== null
        Accessible.ignored: true
        onActiveFocusChanged: if (activeFocus) root.titleItem.forceActiveFocus(Qt.BacktabFocusReason)
    }

    // The bar covers the page: presses on its empty parts must not reach what is under it.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = false
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
        visible: root.pinned
    }

    RowLayout {
        id: band
        x: Theme.section
        width: root.contentWidth
        height: 72
        spacing: 0

        // Pinned, what the bar acts on; it opens from the left edge, easing the button aside.
        Item {
            id: identity

            property real shown: root.pinned ? 1 : 0
            Behavior on shown { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            Layout.preferredWidth: (identityRow.implicitWidth + 24) * shown
            Layout.fillHeight: true
            visible: shown > 0
            opacity: shown
            clip: true

            RowLayout {
                id: identityRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Poster {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 42
                    source: root.posterUrl
                }

                Label {
                    visible: text !== ""
                    Layout.maximumWidth: 200
                    text: root.title
                    color: Theme.ink
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // Layouts fill by default: these two keep their content's width, the figures take the rest.
        RowLayout {
            id: leading
            Layout.fillWidth: false
            spacing: 8
        }

        RowLayout {
            id: trailingRow
            Layout.fillWidth: false
            Layout.leftMargin: leading.implicitWidth > 0 ? 8 : 0
            spacing: 4
        }

        // Where the figures go while they fit beside the actions.
        Item {
            id: figureArea
            // Apart from the actions, or on the edge where there are none.
            Layout.leftMargin: leading.implicitWidth + trailingRow.implicitWidth > 0 ? 28 : 0
            Layout.rightMargin: 16
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
        }
    }

    Flow {
        id: figureFlow

        // Their width side by side, measured from their texts, so it holds before they exist.
        readonly property real needed: {
            let used = 0
            for (let i = 0; i < root.figures.length; ++i) {
                const figure = root.figures[i]
                const action = (figure.action ?? "") !== "" ? 44 : 0
                const width = Math.max(captionMetrics.advanceWidth(figure.caption),
                                       valueMetrics.advanceWidth(figure.value) + action,
                                       figure.progress >= 0 ? 96 : 0)
                used += (i > 0 ? spacing : 0) + Math.ceil(width) + 2
            }
            return used
        }

        x: root.stacked ? band.x : band.x + figureArea.x
        y: root.stacked ? 72 - 8 : (72 - height) / 2
        width: root.stacked ? root.contentWidth : figureArea.width
        spacing: 28

        Repeater {
            model: root.figures

            delegate: Figure {}
        }
    }

    // The figures' fonts, from a label of the style.
    Label {
        id: probe
        visible: false
    }

    FontMetrics {
        id: captionMetrics
        font.family: probe.font.family
        font.pixelSize: Theme.caption
    }

    FontMetrics {
        id: valueMetrics
        font.family: probe.font.family
        font.pixelSize: Theme.body
        font.weight: Font.DemiBold
    }

    Item {
        id: tail
        activeFocusOnTab: root.titleItem !== null
        Accessible.ignored: true
        onActiveFocusChanged: {
            if (activeFocus)
                root.titleItem.nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason)
        }
    }
}
