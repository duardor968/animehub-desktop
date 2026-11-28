import QtQuick
import QtQuick.Controls
import AnimeHub

// An anime as a poster with its title on up to two lines and one line under it; a title cut
// even so shows whole in a tooltip. Hovered, the poster offers what the card is for: play the
// episode or download it. Enter opens it, as a click does; a card reached by keyboard comes into
// view in the page that scrolls it.
AbstractButton {
    id: root

    property url posterUrl
    property string caption
    // A second line under the caption ("Siguiente: Ep. 5"), for a page that needs one.
    property string detail
    property real progress: -1
    property bool fresh: false
    // In the library: a flush vertical bookmark ribbon on the top-right corner of the poster.
    property bool owned: false
    // "play", "download" or nothing: the centered frosted action disc over the poster.
    property string quickAction

    readonly property bool cardHovered: root.hovered || cardHover.hovered || actionBtn.hovered
                                        || ownedHover.hovered
    readonly property bool showAction: root.quickAction !== ""
                                       && (cardHovered || (root.activeFocus && InputModality.keyboard))

    // The poster's own button was pressed, not the card.
    signal act()

    // The page around it scrolls it fully into view (a horizontal row pages by itself).
    function reveal() {
        for (let item = parent; item; item = item.parent) {
            if (item.contentY === undefined || item.contentHeight <= item.height)
                continue
            const top = mapToItem(item.contentItem, 0, 0).y
            const margin = 12
            let target = item.contentY
            if (top - margin < item.contentY)
                target = top - margin
            else if (top + height + margin > item.contentY + item.height)
                target = top + height + margin - item.height
            item.contentY = Math.max(item.originY,
                                     Math.min(target, item.originY + item.contentHeight - item.height))
            return
        }
    }

    // Two lines of title and the caption, so cards of a row share one height.
    implicitWidth: 156
    implicitHeight: posterFrame.height + titleLabel.height + captionLabel.height + 14
                    + (detail !== "" ? detailLabel.height + 2 : 0)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: (caption ? text + ", " + caption : text) + (owned ? ", en tu biblioteca" : "")
    Accessible.description: root.quickAction !== "" ? (actionBtn.text + " (Mayús+Intro)") : ""
    Accessible.onPressAction: clicked()
    Keys.onReturnPressed: event => {
        event.accepted = true
        if ((event.modifiers & Qt.ShiftModifier) && root.quickAction !== "")
            root.act()
        else
            root.clicked()
    }
    Keys.onEnterPressed: event => {
        event.accepted = true
        if ((event.modifiers & Qt.ShiftModifier) && root.quickAction !== "")
            root.act()
        else
            root.clicked()
    }
    onActiveFocusChanged: if (activeFocus && InputModality.keyboard) reveal()

    background: null
    ToolTip.visible: (root.hovered || (root.activeFocus && InputModality.keyboard))
                     && !actionBtn.hovered && !ownedHover.hovered && titleLabel.truncated
    ToolTip.delay: Theme.tooltipDelay
    ToolTip.timeout: Theme.tooltipTimeout
    ToolTip.text: text

    HoverHandler { id: cardHover }

    contentItem: Item {
        // Clipped poster container so hover zoom stays strictly inside the card bounds
        // (eliminating left-edge clipping inside GridView).
        Item {
            id: posterFrame
            width: parent.width
            height: Math.round(width * 1.42)
            clip: true

            Poster {
                id: poster
                anchors.fill: parent
                source: root.posterUrl
                scale: root.cardHovered ? 1.03 : 1.0
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }

            // Gentle dark poster scrim on hover/focus when a quick action is available.
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.34)
                opacity: root.showAction ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }

            // The new episode stands on the poster, as Steam's badges do.
            Label {
                visible: root.fresh
                x: 6
                y: 6
                leftPadding: 6
                rightPadding: 6
                topPadding: 2
                bottomPadding: 2
                text: "Nuevo"
                color: Theme.accentInk
                font.pixelSize: Theme.caption
                font.weight: Font.DemiBold
                background: Rectangle {
                    radius: 3
                    color: Theme.accent
                }
            }

            // Flush top-right #2f81f7 vertical bookmark ribbon with subtle dark outer contrast shadow.
            Item {
                visible: root.owned
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: -2
                anchors.rightMargin: 10
                width: 26
                height: 32
                ToolTip.visible: ownedHover.hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: "En tu biblioteca"

                HoverHandler { id: ownedHover }

                Image {
                    x: 0
                    y: 1
                    width: 26
                    height: 32
                    sourceSize.width: Math.max(26, Math.ceil(26 * Screen.devicePixelRatio))
                    sourceSize.height: Math.max(32, Math.ceil(32 * Screen.devicePixelRatio))
                    smooth: true
                    opacity: 0.65
                    source: root.owned ? Theme.source(Theme.icon.bookmarkRibbon + "?color=080b10") : ""
                    Accessible.ignored: true
                }

                Image {
                    x: 1
                    y: 0
                    width: 24
                    height: 30
                    sourceSize.width: Math.max(24, Math.ceil(24 * Screen.devicePixelRatio))
                    sourceSize.height: Math.max(30, Math.ceil(30 * Screen.devicePixelRatio))
                    smooth: true
                    source: root.owned ? Theme.source(Theme.icon.bookmarkRibbon + "?color=2f81f7") : ""
                    Accessible.ignored: true
                }
            }

            // Centered 44×44 px translucent frosted play/download disc (Ronda 1 B), transitioning
            // smoothly to #2f81f7 when directly hovered.
            AbstractButton {
                id: actionBtn
                anchors.centerIn: parent
                width: 44
                height: 44
                opacity: root.showAction ? 1 : 0
                visible: opacity > 0
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                text: root.quickAction === "play" ? "Ver" : "Descargar"
                Accessible.role: Accessible.Button
                Accessible.name: text + ", " + root.text
                Accessible.onPressAction: root.act()
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                onClicked: root.act()
                Behavior on opacity { NumberAnimation { duration: 140 } }

                background: Rectangle {
                    radius: 22
                    color: actionBtn.down ? "#246bd1"
                           : (actionBtn.hovered ? Theme.accent : Qt.rgba(16 / 255, 20 / 255, 28 / 255, 0.78))
                    border.width: 1
                    border.color: actionBtn.hovered ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                contentItem: Item {
                    Image {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        sourceSize.width: Math.max(18, Math.ceil(18 * Screen.devicePixelRatio))
                        sourceSize.height: Math.max(18, Math.ceil(18 * Screen.devicePixelRatio))
                        smooth: true
                        source: root.quickAction !== ""
                                ? Theme.source((root.quickAction === "play" ? Theme.icon.play
                                                                            : Theme.icon.download)
                                               + "?color=ffffff")
                                : ""
                        Accessible.ignored: true
                    }
                }
            }

            ProgressLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: root.progress >= 0
                value: root.progress
                radius: 0
            }
        }

        // The keyboard's frame, as every control's: around the poster, which is the card's shape.
        // Also when the arrows of a row or a grid bring the keyboard here, which Qt does not
        // count as visual focus; never after a click.
        FocusFrame {
            target: posterFrame
            radiusInside: 0
            visible: root.activeFocus && InputModality.keyboard
        }

        Label {
            id: titleLabel
            anchors.top: posterFrame.bottom
            anchors.topMargin: 8
            width: parent.width
            text: root.text
            // Always two lines high, so the captions of a row line up.
            height: Math.ceil(2 * titleMetrics.lineSpacing) + 4
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop

            FontMetrics {
                id: titleMetrics
                font: titleLabel.font
            }
        }

        // Right under the title, one line or two.
        Label {
            id: captionLabel
            y: titleLabel.y + titleLabel.contentHeight + 4
            width: parent.width
            text: root.caption
            color: Theme.muted
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
        }

        Label {
            id: detailLabel
            y: captionLabel.y + captionLabel.height + 2
            width: parent.width
            visible: root.detail !== ""
            text: root.detail
            color: Theme.muted
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
        }
    }
}
