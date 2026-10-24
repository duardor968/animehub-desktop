import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// An episode of an unfolded series: its still, where its download is and what can be done.
// Its columns line up with the series row above it.
Item {
    id: root

    required property var taskId
    required property string label
    required property url image
    required property string taskState
    required property real progress
    required property string detail
    required property bool problem
    required property bool hasFile
    required property string engineLine
    required property bool canPause
    required property bool canResume
    required property bool canRetry
    required property string partial

    required property DownloadsView downloads
    property int indent
    property int barWidth
    property int actionsWidth

    signal cancelRequested(var taskId, string label, string partial)

    readonly property bool completed: taskState === "completed"
    readonly property bool failed: taskState === "failed"
    readonly property bool running: taskState === "resolving" || taskState === "downloading"
                                    || taskState === "verifying"

    // One more line when JDownloader downloads it: who and where.
    implicitHeight: engineLine !== "" ? 72 : 52
    Accessible.role: Accessible.ListItem
    Accessible.name: label + ", " + detail + (engineLine !== "" ? ", " + engineLine : "")

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.indent
        anchors.rightMargin: 32
        spacing: 0

        Poster {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 36
            source: root.image
            radius: 4
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.leftMargin: 16
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: root.label
                color: Theme.ink
                font.pixelSize: Theme.body
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: root.detail
                color: root.problem ? Theme.danger : Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
                ToolTip.visible: detailHover.hovered && truncated
                ToolTip.text: text
                HoverHandler { id: detailHover }
            }

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

        Item {
            Layout.preferredWidth: root.barWidth
            Layout.minimumWidth: root.barWidth
            Layout.leftMargin: 24
            implicitHeight: 4

            ProgressLine {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                // Only what has started: a column of empty tracks says nothing.
                visible: !root.completed && !root.failed && (root.progress > 0 || root.running)
                value: root.progress
            }
        }

        Row {
            Layout.preferredWidth: root.actionsWidth
            Layout.minimumWidth: root.actionsWidth
            Layout.leftMargin: 16
            layoutDirection: Qt.RightToLeft
            spacing: 4

            // A JDownloader hand-over left unconfirmed (failed, no retry) stays in the list:
            // asked for again, it could reach JDownloader twice.
            ToolButton {
                visible: root.completed ? root.hasFile : !(root.failed && !root.canRetry)
                icon.source: Theme.source(root.completed ? Theme.icon.folder : Theme.icon.cancel)
                display: AbstractButton.IconOnly
                text: root.completed ? "Mostrar en la carpeta"
                                     : root.failed ? "Quitar de la lista" : "Cancelar la descarga"
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: {
                    if (root.completed)
                        root.downloads.showFile(root.taskId)
                    else if (root.failed)
                        root.downloads.forget(root.taskId)
                    else
                        root.cancelRequested(root.taskId, root.label, root.partial)
                }
            }

            ToolButton {
                visible: root.canPause || root.canResume || root.canRetry
                         || (root.completed && root.hasFile)
                icon.source: Theme.source(root.completed ? Theme.icon.play
                                          : root.failed ? Theme.icon.retry
                                          : root.canPause ? Theme.icon.pause : Theme.icon.download)
                display: AbstractButton.IconOnly
                text: root.completed ? "Ver"
                      : root.failed ? "Reintentar"
                      : root.canPause ? "Pausar" : "Reanudar"
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: {
                    if (root.completed)
                        root.downloads.play(root.taskId)
                    else if (root.failed)
                        root.downloads.retry(root.taskId)
                    else if (root.canPause)
                        root.downloads.pause(root.taskId)
                    else
                        root.downloads.resume(root.taskId)
                }
            }
        }
    }
}
