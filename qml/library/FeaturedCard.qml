import QtQuick
import QtQuick.Controls
import AnimeHub

// The next episode to watch, two posters wide at the head of "Para ver": the anime's backdrop
// (its poster when it has none) with the button that plays it, and the same two lines under it
// as the posters beside it.
AbstractButton {
    id: root

    // LibraryModel.featured
    required property var featured
    // The height of the posters beside it, so the lines under both line up.
    required property real imageHeight

    signal open(string slug)
    signal act(string slug)

    readonly property bool hasBackdrop: backdrop.status === Image.Ready

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    text: featured.title ?? ""
    Accessible.name: text + ", " + (featured.detail ?? "")
    background: null
    onClicked: open(featured.slug)
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
            scale: root.hovered ? 1.015 : 1
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

            Button {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                highlighted: true
                text: root.featured.actionText ?? ""
                icon.source: Theme.source(root.featured.action === LibraryModel.Play ? Theme.icon.play
                                                                                      : Theme.icon.download)
                focusPolicy: Qt.NoFocus
                onClicked: root.act(root.featured.slug)
                Accessible.name: text + ", " + root.text
            }

            ProgressLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: (root.featured.progress ?? -1) >= 0
                value: root.featured.progress ?? 0
                radius: 0
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.accent
                border.width: 2
                visible: root.visualFocus
            }
        }

        Label {
            id: titleLabel
            anchors.top: frame.bottom
            anchors.topMargin: 8
            width: parent.width
            text: root.text
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }

        Label {
            anchors.top: titleLabel.bottom
            anchors.topMargin: 2
            width: parent.width
            text: root.featured.detail ?? ""
            color: Theme.muted
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
        }
    }
}
