import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A series on the Downloads page: its poster, how far its episodes are and its actions. The
// whole row unfolds it into its episodes, its chevron on the right as in WinUI's Expander; in
// "A continuación" it can be dragged to another place in line.
Item {
    id: root

    required property int index
    required property string slug
    required property string title
    required property url poster
    required property string summary
    required property real progress
    required property int failed
    required property string failedText
    required property bool canPause
    required property bool canResume
    required property bool canRetry
    required property string engineLine
    required property bool first
    required property string cancelText
    required property int filesOnDisk
    required property string diskText
    required property string watchText
    required property bool hasFolder
    required property var episodes

    required property DownloadsView downloads
    // In "A continuación"; otherwise in "Completadas".
    property bool queued: true
    property bool expanded
    property int barWidth: 200
    // How far it steps aside while another row is dragged over it.
    property real displacement
    readonly property int actionsWidth: 84
    // The chevron's column, past the actions; episode lines keep it free to line up.
    readonly property int chevronRoom: 24
    // Narrow windows: actions keep their icon and say their name in a tooltip.
    readonly property bool compact: barWidth < 160
    // Where episodes start: under the title, past the poster.
    readonly property int indent: 32 + 50 + 16

    signal toggled()
    signal menuRequested(Item anchor)
    signal episodeCancelRequested(var taskId, string label, string partial)
    signal moveRequested(int step)
    signal dragFinished()

    readonly property bool dragging: drag.active
    // How far the row follows the pointer; it eases back once dropped, while the list moves
    // it to its new place.
    readonly property real dragOffset: shift.y

    width: ListView.view ? ListView.view.width : 0
    implicitHeight: header.height + episodesBox.height
    z: dragging || settle.running ? 2 : 0

    transform: [
        Translate { id: shift },
        Translate {
            y: root.displacement
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
    ]

    NumberAnimation {
        id: settle
        target: shift
        property: "y"
        to: 0
        duration: 200
        easing.type: Easing.OutCubic
    }

    AbstractButton {
        id: header
        width: parent.width
        height: 88
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: root.title + ", " + root.summary + (root.failed > 0 ? ", " + root.failedText : "")
                         + (root.engineLine !== "" ? ", " + root.engineLine : "")
        Accessible.description: root.expanded ? "Desplegada" : "Plegada"
        Accessible.onPressAction: root.toggled()
        onClicked: root.toggled()

        Keys.onPressed: event => {
            if (!root.queued || !(event.modifiers & Qt.AltModifier))
                return
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                root.moveRequested(event.key === Qt.Key_Up ? -1 : 1)
                event.accepted = true
            }
        }

        DragHandler {
            id: drag
            enabled: root.queued
            target: null
            xAxis.enabled: false
            grabPermissions: PointerHandler.CanTakeOverFromAnything | PointerHandler.TakeOverForbidden
            cursorShape: Qt.ClosedHandCursor
            // The row follows the pointer within the list, never over its heading.
            onActiveTranslationChanged: {
                if (!active || !root.ListView.view)
                    return
                const room = root.ListView.view.contentHeight - root.height
                shift.y = Math.max(-root.y, Math.min(room - root.y, activeTranslation.y))
            }
            onActiveChanged: {
                if (active) {
                    settle.stop()
                } else {
                    root.dragFinished()
                    settle.restart()
                }
            }
        }

        background: Rectangle {
            x: 16
            width: parent.width - 32
            radius: Theme.radius
            color: root.dragging ? Theme.raised : header.hovered ? Theme.hover : "transparent"
            border.width: header.visualFocus ? 2 : root.dragging ? 1 : 0
            border.color: header.visualFocus ? Theme.accent : Theme.stroke
        }

        contentItem: RowLayout {
            spacing: 0

            Poster {
                Layout.leftMargin: 32
                Layout.preferredWidth: 50
                Layout.preferredHeight: 71
                source: root.poster
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.leftMargin: 16
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.ink
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                // The summary gives way first when the row is narrow: the failures and their
                // action stay whole. As high as its text: the action stands out of it, so the
                // line sits where every row's does.
                Item {
                    id: meta
                    Layout.fillWidth: true
                    implicitHeight: summaryLabel.implicitHeight

                    Row {
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            id: summaryLabel
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, meta.width
                                            - (failures.visible
                                               ? separator.implicitWidth + failures.implicitWidth : 0)
                                            - (retry.visible ? retry.width + 4 : 0))
                            text: root.summary
                            color: Theme.muted
                            font.pixelSize: Theme.body
                            elide: Text.ElideRight
                        }

                        Label {
                            id: separator
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.failed > 0
                            text: " · "
                            color: Theme.muted
                            font.pixelSize: Theme.body
                        }

                        Label {
                            id: failures
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.failed > 0
                            text: root.failedText
                            color: Theme.danger
                            font.pixelSize: Theme.body
                        }

                        Item {
                            visible: retry.visible
                            width: 4
                            height: 1
                        }

                        // A tool button: as narrow as its icon alone in narrow windows.
                        ToolButton {
                            id: retry
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.canRetry
                            display: root.compact ? AbstractButton.IconOnly
                                                  : AbstractButton.TextOnly
                            text: "Reintentar fallidos"
                            font.pixelSize: Theme.body
                            icon.source: Theme.source(Theme.icon.retry)
                            ToolTip.visible: hovered && root.compact
                            ToolTip.text: text
                            onClicked: root.downloads.retryFailed(root.slug)
                        }
                    }
                }
                // Who downloads it and where, only when JDownloader takes part.
                Label {
                    Layout.fillWidth: true
                    visible: root.engineLine !== ""
                    text: root.engineLine
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideMiddle
                    ToolTip.visible: engineHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: engineHover }
                }
            }

            // Fixed columns, so bars and actions line up from row to row.
            Item {
                Layout.preferredWidth: root.barWidth
                Layout.minimumWidth: root.barWidth
                Layout.leftMargin: 24
                Layout.fillHeight: true

                ProgressLine {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    implicitHeight: 4
                    visible: root.queued
                    value: root.progress
                }

                Button {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.queued && root.watchText !== "" && !root.compact
                    text: root.watchText
                    icon.source: Theme.source(Theme.icon.play)
                    onClicked: root.downloads.watchNext(root.slug)
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.queued && root.watchText !== "" && root.compact
                    display: AbstractButton.IconOnly
                    text: root.watchText
                    icon.source: Theme.source(Theme.icon.play)
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: root.downloads.watchNext(root.slug)
                }
            }

            Row {
                Layout.preferredWidth: root.actionsWidth
                Layout.minimumWidth: root.actionsWidth
                Layout.leftMargin: 16
                layoutDirection: Qt.RightToLeft
                spacing: 4

                ToolButton {
                    id: more
                    icon.source: Theme.source(Theme.icon.more)
                    display: AbstractButton.IconOnly
                    text: "Más opciones"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: root.menuRequested(more)
                }

                ToolButton {
                    visible: root.queued ? root.canPause || root.canResume : true
                    icon.source: Theme.source(!root.queued ? Theme.icon.folder
                                              : root.canPause ? Theme.icon.pause : Theme.icon.download)
                    display: AbstractButton.IconOnly
                    text: !root.queued ? "Abrir la carpeta" : root.canPause ? "Pausar" : "Reanudar"
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: {
                        if (!root.queued)
                            root.downloads.openSeriesFolder(root.slug)
                        else if (root.canPause)
                            root.downloads.pauseSeries(root.slug)
                        else
                            root.downloads.resumeSeries(root.slug)
                    }
                }
            }

            Glyph {
                Layout.preferredWidth: root.chevronRoom - 8
                Layout.leftMargin: 8
                Layout.rightMargin: 32
                glyph: root.expanded ? "e70e" : "e70d"
                size: 12
                color: Theme.muted
            }
        }
    }

    Item {
        id: episodesBox
        anchors.top: header.bottom
        width: parent.width
        height: root.expanded ? episodeColumn.implicitHeight : 0
        clip: true
        opacity: root.expanded ? 1 : 0
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Column {
            id: episodeColumn
            width: parent.width
            bottomPadding: 8

            Repeater {
                // Built while unfolded or folding, so a folded series costs nothing.
                model: root.expanded || episodesBox.height > 0 ? root.episodes : null

                EpisodeLine {
                    width: episodeColumn.width
                    downloads: root.downloads
                    indent: root.indent
                    barWidth: root.barWidth
                    actionsWidth: root.actionsWidth
                    trailing: root.chevronRoom
                    onCancelRequested: (taskId, label, partial) =>
                        root.episodeCancelRequested(taskId, label, partial)
                }
            }
        }
    }
}
