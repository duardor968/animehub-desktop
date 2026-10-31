import QtQuick
import QtQuick.Controls
import AnimeHub

// The next episode to watch, two posters wide at the head of "Para ver": the anime's backdrop
// (its poster when it has none) with the button that plays it, and the same two lines under it
// as the posters beside it. The card opens the anime and its button plays the episode: two
// controls side by side, each a stop for the keyboard, not a button inside another.
Item {
    id: root

    // LibraryModel.featured
    required property var featured
    // The height of the posters beside it, so the lines under both line up.
    required property real imageHeight

    signal open(string slug)
    signal act(string slug)
    // → from the card: on to the posters beside it.
    signal next()

    readonly property bool hasBackdrop: backdrop.status === Image.Ready

    // The keyboard arrives here from the row (← from its first poster): the card takes it.
    function forceFocus(reason) { card.forceActiveFocus(reason) }

    AbstractButton {
        id: card

        anchors.fill: parent
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        text: root.featured.title ?? ""
        Accessible.name: text + ", " + (root.featured.detail ?? "")
        background: null
        onClicked: root.open(root.featured.slug)
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onRightPressed: root.next()
        ToolTip.visible: hovered && titleLabel.truncated
        ToolTip.delay: 500
        ToolTip.text: text

        contentItem: Item {
            Rectangle {
                id: frame
                width: parent.width
                height: root.imageHeight
                color: Theme.raised
                clip: true
                scale: card.hovered || play.hovered ? 1.015 : 1
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                // Without a backdrop the poster fills the frame, cropped like one.
                Poster {
                    anchors.fill: parent
                    visible: !root.hasBackdrop
                    source: root.featured.posterUrl ?? ""
                }

                Image {
                    id: backdrop
                    anchors.fill: parent
                    source: root.featured.backdropUrl ?? ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }

                // The button reads over any picture: the frame darkens towards it.
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.5; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.55) }
                    }
                }

                ProgressLine {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    visible: (root.featured.progress ?? -1) >= 0
                    value: root.featured.progress ?? 0
                    radius: 0
                }
            }

            // The keyboard's frame, as on the posters beside it.
            FocusFrame {
                target: frame
                radiusInside: 0
                visible: card.activeFocus && card.focusReason !== Qt.MouseFocusReason
            }

            Label {
                id: titleLabel
                anchors.top: frame.bottom
                anchors.topMargin: 8
                width: parent.width
                text: card.text
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

            // Right under the title, as on the posters beside it.
            Label {
                y: titleLabel.y + titleLabel.contentHeight + 4
                width: parent.width
                text: root.featured.detail ?? ""
                color: Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
            }
        }
    }

    // Over the picture's lower left corner, after the card in the Tab order.
    Button {
        id: play
        x: 12
        y: root.imageHeight - height - 12
        highlighted: true
        text: root.featured.actionText ?? ""
        icon.source: Theme.source(Theme.icon.play)
        onClicked: root.act(root.featured.slug)
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onRightPressed: root.next()
        Accessible.name: text + ", " + card.text
    }
}
