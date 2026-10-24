import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The top of the Downloads page, as in Steam: the series on view over its art, with its
// episode, progress, failures and actions; on the right, the speed of the last minutes, whose
// graph keeps its place at rest, and in the corner what acts on every download. The series is
// not repeated in the line below: it unfolds into its episodes from here.
Rectangle {
    id: root

    required property DownloadsView downloads
    // The series on view is unfolded into its episodes, under the header.
    property bool expanded

    signal toggled(string slug)
    signal menuRequested(var row, Item anchor)
    signal folderRequested()

    readonly property int mode: downloads.headerMode
    readonly property bool idle: mode === DownloadsView.Idle
    readonly property bool wide: width >= 1280
    // Room at the top for the corner's buttons.
    readonly property int topRoom: pageActions.y + pageActions.height + 16

    // Without a download on view there is no art: the band keeps the speed and its graph.
    implicitHeight: Math.max(topRoom + speed.implicitHeight + 28,
                             idle ? 0 : Math.max(200, topRoom + subject.implicitHeight + 28))
    Behavior on implicitHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    color: Theme.pane
    clip: true

    // A figure of the session: its value over its name, right-aligned in a third of the block,
    // so the last one ends where the rows below do and none moves as its value changes.
    component Figure: ColumnLayout {
        property alias value: valueLabel.text
        property alias label: nameLabel.text
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        spacing: 2
        Accessible.role: Accessible.StaticText
        Accessible.name: label + ": " + value

        // Filling, so the column can take its third: a layout grows no wider than its children.
        Label {
            id: valueLabel
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }
        Label {
            id: nameLabel
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
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

        // Who downloads the series and where, only when JDownloader takes part.
        Repeater {
            model: root.idle ? null : root.downloads.current

            Label {
                required property string engineLine
                Layout.fillWidth: true
                visible: engineLine !== ""
                text: engineLine
                color: Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideMiddle
                ToolTip.visible: engineHover.hovered && truncated
                ToolTip.text: text
                HoverHandler { id: engineHover }
            }
        }
        Repeater {
            model: root.idle ? null : root.downloads.current

            RowLayout {
                required property string slug
                required property bool canRetry
                required property int failed
                required property string failedText

                visible: failed > 0
                spacing: 4

                Label {
                    text: parent.failedText
                    color: Theme.danger
                    font.pixelSize: Theme.body
                }
                Button {
                    visible: parent.canRetry
                    flat: true
                    text: "Reintentar fallidos"
                    onClicked: root.downloads.retryFailed(parent.slug)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            visible: !root.idle
            spacing: 16

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

            Repeater {
                model: root.idle ? null : root.downloads.current

                // What the series rows below offer, for the series on view.
                RowLayout {
                    id: actions

                    required property string slug
                    required property string title
                    required property bool first
                    required property bool hasFolder
                    required property string cancelText

                    spacing: 4

                    // One label for both states, so nothing beside it moves; the chevron turns.
                    ToolButton {
                        text: "Episodios"
                        font.pixelSize: Theme.body
                        icon.source: Theme.source(root.expanded ? "e70e" : "e70d")
                        Accessible.description: root.expanded ? "Desplegados" : "Plegados"
                        onClicked: root.toggled(actions.slug)
                    }
                    ToolButton {
                        id: more
                        icon.source: Theme.source(Theme.icon.more)
                        display: AbstractButton.IconOnly
                        text: "Más opciones"
                        ToolTip.visible: hovered
                        ToolTip.text: text
                        onClicked: root.menuRequested(actions, more)
                    }
                }
            }
        }
    }

    // What acts on every download, as Steam's and as the bottom bar's and the tray's.
    RowLayout {
        id: pageActions
        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.top: parent.top
        anchors.topMargin: 16
        spacing: 4

        Button {
            visible: root.downloads.canPauseAll || root.downloads.canResumeAll
            text: root.downloads.canPauseAll ? "Pausar todo" : "Reanudar todo"
            icon.source: Theme.source(root.downloads.canPauseAll ? Theme.icon.pause
                                                                 : Theme.icon.download)
            onClicked: root.downloads.canPauseAll ? root.downloads.pauseAll()
                                                  : root.downloads.resumeAll()
        }

        ToolButton {
            id: pageMore
            icon.source: Theme.source(Theme.icon.more)
            display: AbstractButton.IconOnly
            text: "Más opciones de Descargas"
            ToolTip.visible: hovered
            ToolTip.text: "Más opciones"
            onClicked: pageMenu.show(pageMore, true)
        }
    }

    ActionMenu {
        id: pageMenu

        MenuEntry {
            text: "Abrir la carpeta de descargas"
            glyph: Theme.icon.folder
            onTriggered: root.folderRequested()
        }
    }

    // The speed as one block: its graph over the session's figures, in equal columns.
    ColumnLayout {
        id: speed
        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: root.wide ? 312 : 252
        spacing: 10

        // Narrow windows keep the series' line whole: the figures stay, the graph goes.
        SpeedGraph {
            visible: root.width >= 1000
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            samples: root.downloads.speedSamples
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            Figure { value: root.downloads.speedText; label: "Actual" }
            Figure { value: root.downloads.peakText; label: "Máximo" }
            Figure { value: root.downloads.totalText; label: "Total" }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }
}
