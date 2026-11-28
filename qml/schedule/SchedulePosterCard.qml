import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import AnimeHub

// Cohesive elevated poster card for the Horario day grid:
// - Rounded 8 px card on Theme.surface (#151922) with subtle 1 px hairline border & hover lift
// - Top-left integrated corner time tag on dark translucent backing (bottom-right 7 px radius)
// - Top-right flush #2f81f7 vertical bookmark-ribbon ONLY when followed === true
// - Bottom-left corner EP tag seamlessly sharing cardSurface (1 px overlap eliminates HiDPI seams)
// - Fixed-height 2-line title + ScheduleStateChip (Final 100% identical soft green to Emitido)
AbstractButton {
    id: root

    required property string title
    required property string epNumber
    required property string timeText
    required property string status
    property string stateLabel: ""
    required property bool followed
    required property url poster

    signal activated()

    readonly property int posterHeight: Math.round(width * 1.36)
    readonly property int bodyHeight: 88
    readonly property bool cardHovered: root.hovered || ribbonHover.hovered
    readonly property color cardSurface: cardHovered ? Theme.raised : Theme.surface

    implicitWidth: 180
    implicitHeight: posterHeight + bodyHeight
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    text: title
    Accessible.name: title
                     + (epNumber !== "" ? ", episodio " + epNumber : "")
                     + (timeText !== "" ? ", " + timeText : "")
                     + (stateLabel !== "" ? ", " + stateLabel : "")
                     + (followed ? ", en tu biblioteca" : "")
    Accessible.onPressAction: root.clicked()
    onClicked: root.activated()
    Keys.onReturnPressed: event => {
        event.accepted = true
        root.clicked()
    }
    Keys.onEnterPressed: event => {
        event.accepted = true
        root.clicked()
    }
    Keys.onSpacePressed: event => {
        event.accepted = true
        root.clicked()
    }
    ToolTip.visible: (root.hovered || (root.activeFocus && InputModality.keyboard))
                     && !ribbonHover.hovered && titleLbl.truncated
    ToolTip.delay: Theme.tooltipDelay
    ToolTip.timeout: Theme.tooltipTimeout
    ToolTip.text: title

    background: null

    contentItem: Item {
        // Smooth 8 px rounded mask so poster top corners clip cleanly inside the card frame.
        Rectangle {
            id: cardMask
            anchors.fill: parent
            radius: Theme.radiusLg
            visible: false
            layer.enabled: root.width > 0 && root.height > 0
        }

        Item {
            anchors.fill: parent
            layer.enabled: root.width > 0 && root.height > 0
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: cardMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
                autoPaddingEnabled: false
            }

            // Unified card background surface.
            Rectangle {
                anchors.fill: parent
                color: root.cardSurface
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            // Top Poster Area.
            Item {
                id: posterArea
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: root.posterHeight
                clip: true

                Poster {
                    anchors.fill: parent
                    source: root.poster
                    scale: root.cardHovered ? 1.035 : 1.0
                    Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                }

                // Subtle bottom micro-scrim so poster meets the card body with depth.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 34
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.34) }
                    }
                }

                // Top-left corner time tag.
                Rectangle {
                    visible: root.timeText !== ""
                    anchors.top: parent.top
                    anchors.left: parent.left
                    height: 26
                    width: timeLbl.implicitWidth + 20
                    topLeftRadius: 0
                    topRightRadius: 0
                    bottomLeftRadius: 0
                    bottomRightRadius: 7
                    color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.88)

                    Label {
                        id: timeLbl
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10
                        text: root.timeText
                        color: Theme.ink
                        font.pixelSize: Theme.caption
                        font.weight: Font.Bold
                        Accessible.ignored: true
                    }
                }

                // Top-right flush #2f81f7 vertical bookmark-ribbon ONLY when followed.
                Item {
                    visible: root.followed
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: -2
                    anchors.rightMargin: 10
                    width: 24
                    height: 30
                    ToolTip.visible: ribbonHover.hovered
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: "En tu biblioteca"

                    HoverHandler { id: ribbonHover }

                    Image {
                        x: 0
                        y: 1
                        width: 24
                        height: 30
                        sourceSize.width: Math.max(24, Math.ceil(24 * Screen.devicePixelRatio))
                        sourceSize.height: Math.max(30, Math.ceil(30 * Screen.devicePixelRatio))
                        smooth: true
                        opacity: 0.72
                        source: root.followed ? Theme.source(Theme.icon.bookmarkRibbon + "?color=080b10") : ""
                        Accessible.ignored: true
                    }

                    Image {
                        x: 1
                        y: 0
                        width: 22
                        height: 28
                        sourceSize.width: Math.max(22, Math.ceil(22 * Screen.devicePixelRatio))
                        sourceSize.height: Math.max(28, Math.ceil(28 * Screen.devicePixelRatio))
                        smooth: true
                        source: root.followed ? Theme.source(Theme.icon.bookmarkRibbon + "?color=2f81f7") : ""
                        Accessible.ignored: true
                    }
                }

                // Bottom-left corner EP tag seamlessly sharing cardSurface (1 px overlap kills HiDPI seams).
                Rectangle {
                    visible: root.epNumber !== ""
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -1
                    height: 27
                    width: epRow.implicitWidth + 22
                    topLeftRadius: 0
                    topRightRadius: Theme.radiusLg
                    bottomLeftRadius: 0
                    bottomRightRadius: 0
                    color: root.cardSurface
                    Behavior on color { ColorAnimation { duration: 120 } }

                    Row {
                        id: epRow
                        anchors.verticalCenter: parent.verticalCenter
                        x: 12
                        spacing: 4

                        Label {
                            anchors.baseline: epVal.baseline
                            text: "EP"
                            color: Theme.accentText
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                            Accessible.ignored: true
                        }

                        Label {
                            id: epVal
                            text: root.epNumber
                            color: Theme.ink
                            font.pixelSize: Theme.caption
                            font.weight: Font.Bold
                            Accessible.ignored: true
                        }
                    }
                }
            }

            // Integrated Card Body (same surface, fixed title height for crisp grid alignment).
            Item {
                anchors.top: posterArea.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom

                Label {
                    id: titleLbl
                    x: 12
                    y: 9
                    width: parent.width - 24
                    text: root.title
                    color: root.cardHovered ? Theme.accentInk : Theme.ink
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                    lineHeight: 1.18
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                ScheduleStateChip {
                    id: stateChip
                    x: 12
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    status: root.status
                    label: root.stateLabel
                }
            }
        }

        // Crisp outer 1 px hairline border framing the entire rounded card.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: "transparent"
            border.width: 1
            border.color: root.cardHovered ? Qt.rgba(1, 1, 1, 0.20) : Theme.pressSubtle
            Behavior on border.color { ColorAnimation { duration: 120 } }
        }

        FocusFrame {
            radiusInside: Theme.radiusLg
            visible: root.activeFocus && InputModality.keyboard
        }
    }
}
