import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The top of the Downloads page, as in Steam: the series on view over its art, with its
// episode, progress and pause; on the right, the speed of the last minutes.
Rectangle {
    id: root

    required property DownloadsView downloads

    readonly property int mode: downloads.headerMode
    readonly property bool idle: mode === DownloadsView.Idle
    readonly property bool paused: mode === DownloadsView.Paused
    readonly property bool wide: width >= 1280

    // Without a download on view there is no art: the band shrinks to the figures.
    implicitHeight: idle ? 136 : 200
    Behavior on implicitHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    color: Theme.pane
    clip: true

    // A figure of the session: its value over its name.
    component Figure: ColumnLayout {
        property alias value: valueLabel.text
        property alias label: nameLabel.text
        Layout.alignment: Qt.AlignBottom
        Layout.minimumWidth: 72
        spacing: 2
        Accessible.role: Accessible.StaticText
        Accessible.name: label + ": " + value

        Label {
            id: valueLabel
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }
        Label {
            id: nameLabel
            color: Theme.muted
            font.pixelSize: Theme.caption
            Accessible.ignored: true
        }
    }

    Image {
        id: art
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(parent.width * 0.62, 860)
        source: root.idle ? "" : root.downloads.headerArt
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        opacity: status === Image.Ready ? 0.5 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
    }

    // Siblings, not children: they must not inherit the image's opacity to reach the pane.
    Rectangle {
        anchors.fill: art
        visible: art.status === Image.Ready
        gradient: Gradient {
            orientation: Gradient.Horizontal
            // Darker under the text on the left, open in the middle, gone into the pane on the right.
            GradientStop { position: 0.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.5) }
            GradientStop { position: 0.4; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.1) }
            GradientStop { position: 1.0; color: Theme.pane }
        }
    }
    Rectangle {
        anchors.fill: art
        visible: art.status === Image.Ready
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.1) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.9) }
        }
    }

    ColumnLayout {
        id: subject
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: Math.min(560, root.width - speed.width - 96)
        spacing: 4

        Label {
            Layout.fillWidth: true
            text: root.downloads.headerTitle
            color: root.idle ? Theme.muted : Theme.ink
            font.pixelSize: root.idle ? Theme.subtitle : Theme.title
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }

        Label {
            Layout.fillWidth: true
            visible: !root.idle
            text: root.downloads.headerEpisode
            color: Theme.muted
            font.pixelSize: Theme.body
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            visible: !root.idle
            spacing: 16

            Button {
                id: toggle
                highlighted: true
                display: AbstractButton.IconOnly
                // The icon and Fluent's own side padding; narrower squeezes the icon.
                implicitWidth: 40
                text: root.paused ? "Reanudar" : "Pausar"
                icon.source: Theme.source(root.paused ? Theme.icon.download : Theme.icon.pause)
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: root.paused ? root.downloads.resumeSeries(root.downloads.headerSlug)
                                       : root.downloads.pauseSeries(root.downloads.headerSlug)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                ProgressLine {
                    Layout.fillWidth: true
                    implicitHeight: 4
                    value: root.downloads.headerProgress
                }

                Label {
                    Layout.fillWidth: true
                    text: root.downloads.headerDetail
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                }
            }
        }
    }

    RowLayout {
        id: speed
        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        spacing: 28

        SpeedGraph {
            Layout.preferredWidth: root.wide ? 260 : 160
            Layout.preferredHeight: 88
            samples: root.downloads.speedSamples
        }

        Figure { value: root.downloads.speedText; label: "Actual" }
        Figure { value: root.downloads.peakText; label: "Máximo" }
        Figure { value: root.downloads.totalText; label: "Total" }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }
}
