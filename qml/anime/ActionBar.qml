import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The band under a page's header, as Steam's under a game: the main action on the left with the
// page's own buttons beside it (favourite, alerts, more), then the figures. Once pinned to the top
// during scroll, it stays strictly single-row (~68 px) so it never jumps taller or slices content.
Rectangle {
    id: root

    // [{caption, value, progress (< 0: no bar), problem, action}]. Beside the actions while they
    // all fit there; when unpinned on a narrow pane they wrap cleanly beneath; when pinned the bar
    // stays strictly single-row.
    property var figures: []
    // Stuck to the top: elevated pane surface and full bottom hairline separate it from scrolling rows.
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

    readonly property int singleRowHeight: 68
    // Narrow pane (e.g. 820x600 with the library sidebar): secondary actions compact so trailing
    // icons never clip.
    readonly property bool compact: width < 760
    readonly property real actionsWidth: leading.implicitWidth
                                         + (leading.implicitWidth > 0 && trailingRow.implicitWidth > 0 ? 8 : 0)
                                         + trailingRow.implicitWidth
    readonly property real identityRoom: Math.max(0, root.contentWidth - actionsWidth)
    readonly property bool showIdentity: root.pinned && root.width >= 680 && identityRoom >= 140
    readonly property real identityTargetWidth: showIdentity
                                                ? Math.min(identityRow.implicitWidth + 24, identityRoom)
                                                : 0
    readonly property real figureGap: actionsWidth > 0 ? (compact ? 16 : 28) : 0
    readonly property real unpinnedFigureWidth: Math.max(0, root.contentWidth - actionsWidth - figureGap)
    readonly property real availableFigureWidth: Math.max(0, root.contentWidth - actionsWidth
                                                          - Math.max(identityTargetWidth, identity.Layout.preferredWidth)
                                                          - figureGap)

    // Unpinned height stays stable for the header's barSlot so pinning never shifts list.contentY.
    readonly property bool unpinnedStacked: figures.length > 0 && figureFlow.needed > unpinnedFigureWidth
    readonly property real unpinnedHeight: unpinnedStacked
                                           ? singleRowHeight + figureFlow.height + 16
                                           : singleRowHeight
    // Strictly single-row while pinned: never flips stacked mid-scroll when showIdentity appears.
    readonly property bool stacked: !root.pinned && unpinnedStacked
    readonly property bool showFigures: figures.length > 0
                                        && (!root.pinned || figureFlow.needed <= availableFigureWidth)

    implicitHeight: root.pinned ? singleRowHeight : unpinnedHeight
    color: root.pinned ? Theme.pane : Theme.canvas
    Behavior on color { ColorAnimation { duration: 140 } }
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
                    ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    Accessible.onPressAction: click()
                    Keys.onReturnPressed: event => { event.accepted = true; click() }
                    Keys.onEnterPressed: event => { event.accepted = true; click() }
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

    // The bar's ends in the Tab order, which hand the keyboard on:
    // - Shift+Tab from the bar's first control lands on `head` (BacktabFocusReason) -> back to title.
    // - Forward Tab past the end of the page lands on `head` (TabFocusReason) -> skip the bar cleanly
    //   so Tab never loops back to the title.
    Control {
        id: head
        width: 0
        height: 0
        padding: 0
        background: null
        contentItem: null
        focusPolicy: Qt.TabFocus
        activeFocusOnTab: root.titleItem !== null
        Accessible.ignored: true
        onActiveFocusChanged: {
            if (!activeFocus || !root.titleItem)
                return
            if (focusReason === Qt.BacktabFocusReason) {
                root.titleItem.forceActiveFocus(Qt.BacktabFocusReason)
            } else {
                const next = tail.nextItemInFocusChain(true)
                if (next && next !== head && next !== tail)
                    next.forceActiveFocus(Qt.TabFocusReason)
            }
        }
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
        opacity: root.pinned ? 1.0 : 0.65
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }

    RowLayout {
        id: band
        x: Theme.section
        width: root.contentWidth
        height: root.singleRowHeight
        spacing: 0

        // Pinned, what the bar acts on; it opens from the left edge, easing the button aside.
        // Hidden on narrow panes (< 680 px) or when space is tight so trailing actions never clip.
        Item {
            id: identity

            property real shown: root.showIdentity ? 1 : 0
            Behavior on shown { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            Layout.preferredWidth: Math.min(identityRow.implicitWidth + 24, root.identityRoom) * shown
            Layout.fillHeight: true
            visible: shown > 0
            opacity: shown
            clip: true

            RowLayout {
                id: identityRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Poster {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 40
                    source: root.posterUrl
                }

                Label {
                    visible: text !== ""
                    Layout.maximumWidth: Math.min(root.compact ? 120 : 200,
                                                  Math.max(0, root.identityRoom - 62))
                    text: root.title
                    color: Theme.ink
                    font.pixelSize: Theme.small
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // Layouts fill by default: these two keep their content's width, the figures take the rest.
        RowLayout {
            id: leading
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
        }

        RowLayout {
            id: trailingRow
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: leading.implicitWidth > 0 ? 8 : 0
            spacing: 4
        }

        // Where the figures go while they fit beside the actions. Zero margin when stacked or
        // empty so narrow panes keep 100 % of contentWidth for actions and trailing buttons.
        Item {
            id: figureArea
            Layout.leftMargin: root.showFigures && !root.stacked ? root.figureGap : 0
            Layout.rightMargin: 0
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

        visible: root.showFigures
        x: root.stacked ? band.x : band.x + figureArea.x
        y: root.stacked ? root.singleRowHeight - 8 : (root.singleRowHeight - height) / 2
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

    Control {
        id: tail
        width: 0
        height: 0
        padding: 0
        background: null
        contentItem: null
        focusPolicy: Qt.TabFocus
        activeFocusOnTab: root.titleItem !== null
        Accessible.ignored: true
        onActiveFocusChanged: {
            if (!activeFocus || !root.titleItem)
                return
            if (focusReason === Qt.BacktabFocusReason) {
                const prev = head.nextItemInFocusChain(false)
                if (prev && prev !== head && prev !== tail)
                    prev.forceActiveFocus(Qt.BacktabFocusReason)
            } else {
                const next = root.titleItem.nextItemInFocusChain(true)
                if (next && next !== head && next !== tail)
                    next.forceActiveFocus(Qt.TabFocusReason)
            }
        }
    }
}
