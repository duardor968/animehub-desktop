import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Downloads at the bottom of every page but Downloads, while something is unfinished: the one
// running, or that they are paused, or that some failed. A click opens Downloads.
AbstractButton {
    id: root

    required property DownloadsView downloads

    readonly property int mode: downloads.barMode

    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: "Descargas: " + downloads.barText
                     + (downloads.barProblem !== "" ? ", " + downloads.barProblem : "")

    background: Rectangle {
        color: root.hovered ? Theme.hover : Theme.pane

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.stroke
        }
    }

    contentItem: RowLayout {
        spacing: 10

        // What the bar is about, not a control: the action is on the right, with its name.
        Glyph {
            Layout.leftMargin: 16
            glyph: root.mode === DownloadsView.Failed ? Theme.icon.warning : Theme.icon.download
            size: 14
            color: root.mode === DownloadsView.Failed ? Theme.danger
                 : root.mode === DownloadsView.Paused ? Theme.muted : Theme.accent
        }

        Label {
            text: root.downloads.barText
            color: Theme.ink
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
        }

        ProgressLine {
            visible: root.mode === DownloadsView.Active
            Layout.preferredWidth: 160
            value: root.downloads.barProgress
        }

        Label {
            visible: text !== ""
            text: root.downloads.barProblem
            color: Theme.danger
            font.pixelSize: Theme.caption
        }

        Item { Layout.fillWidth: true }

        // What JDownloader already transfers is not AnimeHub's to pause: with only that left,
        // there is nothing to pause or resume here.
        Button {
            visible: root.mode === DownloadsView.Failed ? root.downloads.canRetryFailed
                     : root.downloads.canPauseAll || root.downloads.canResumeAll
            Layout.rightMargin: 8
            flat: true
            text: root.mode === DownloadsView.Failed ? "Reintentar"
                  : root.downloads.canPauseAll ? "Pausar" : "Reanudar"
            icon.source: Theme.source(root.mode === DownloadsView.Failed ? Theme.icon.retry
                                      : root.downloads.canPauseAll ? Theme.icon.pause
                                                                   : Theme.icon.download)
            icon.width: 16
            icon.height: 16
            onClicked: {
                if (root.mode === DownloadsView.Failed)
                    root.downloads.retryAllFailed()
                else if (root.downloads.canPauseAll)
                    root.downloads.pauseAll()
                else
                    root.downloads.resumeAll()
            }
        }
    }
}
