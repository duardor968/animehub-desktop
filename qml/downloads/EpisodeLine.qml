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
    required property bool canPause
    required property bool canResume
    required property bool canRetry
    required property bool canRetryNow
    required property string partial

    required property DownloadsView downloads
    property int indent
    property int barWidth
    property int actionsWidth
    // Room left after the actions: the series rows' chevron column.
    property int trailing

    signal cancelRequested(var taskId, string label, string partial)

    readonly property bool completed: taskState === "completed"
    readonly property bool failed: taskState === "failed"
    readonly property bool running: taskState === "resolving" || taskState === "downloading"
                                    || taskState === "verifying"

    implicitHeight: 52
    Accessible.role: Accessible.ListItem
    Accessible.name: label + ", " + detail

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.indent
        anchors.rightMargin: 32 + root.trailing
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
                visible: root.progress > 0 && !root.completed && !root.failed
                value: root.progress
            }
            // Running with nothing measured yet (looking for the file, opening JDownloader).
            ProgressBar {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                // Shown once laid out: Fluent times its sweep by the width it starts with, and
                // one started at width 0 never moves.
                visible: root.running && root.progress === 0 && width > 0
                indeterminate: true
                Accessible.name: root.detail
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

            // Pause only what moves: an episode waiting its turn has nothing to pause yet. One
            // waiting to try again by itself can try now.
            ToolButton {
                visible: (root.canPause && root.running) || root.canResume || root.canRetry
                         || root.canRetryNow || (root.completed && root.hasFile)
                icon.source: Theme.source(root.completed || root.canResume ? Theme.icon.play
                                          : root.failed || root.canRetryNow ? Theme.icon.retry
                                          : Theme.icon.pause)
                display: AbstractButton.IconOnly
                text: root.completed ? "Ver"
                      : root.failed ? "Reintentar"
                      : root.canRetryNow ? "Reintentar ahora"
                      : root.canResume ? "Reanudar" : "Pausar"
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: {
                    if (root.completed)
                        root.downloads.play(root.taskId)
                    else if (root.failed || root.canRetryNow)
                        root.downloads.retry(root.taskId)
                    else if (root.canResume)
                        root.downloads.resume(root.taskId)
                    else
                        root.downloads.pause(root.taskId)
                }
            }
        }
    }
}
