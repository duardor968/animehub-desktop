import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Downloads at the bottom of every page but Downloads, while something is unfinished. In the
// middle, as Steam's: the poster, where the line is, the episode and how far it is, or that
// they are paused, or that some failed. A click opens Downloads.
AbstractButton {
    id: root

    required property DownloadsView downloads

    readonly property int mode: downloads.barMode
    readonly property bool running: mode === DownloadsView.Active
    readonly property bool named: running || mode === DownloadsView.Waiting

    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: "Descargas: " + downloads.barText
                     + (downloads.barPercent !== "" ? ", " + downloads.barPercent : "")
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

    contentItem: Item {
        // Clear of the button on the right on both sides, so it stays in the middle.
        readonly property real room: width - 2 * (action.visible ? action.width + 24 : 24)

        RowLayout {
            id: center
            anchors.centerIn: parent
            // Running, a fixed width keeps the bar's length; otherwise, as wide as its words.
            width: Math.max(0, Math.min(root.running ? 520 : implicitWidth, parent.room))
            spacing: 10

            Poster {
                visible: root.named
                Layout.preferredWidth: 20
                Layout.preferredHeight: 28
                source: root.downloads.barPoster
            }

            // What the bar is about, not a control: the action is on the right, with its name.
            Glyph {
                visible: !root.named
                glyph: root.mode === DownloadsView.Failed ? Theme.icon.warning : Theme.icon.pause
                size: 14
                color: root.mode === DownloadsView.Failed ? Theme.danger : Theme.muted
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    // The title gives way in the middle: where the line is and the episode stay.
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.downloads.barText
                        color: Theme.ink
                        font.pixelSize: Theme.caption
                        elide: Text.ElideMiddle
                    }

                    Label {
                        visible: text !== ""
                        text: root.downloads.barProblem
                        color: Theme.danger
                        font.pixelSize: Theme.caption
                    }

                    Label {
                        visible: text !== ""
                        text: root.downloads.barPercent
                        color: Theme.muted
                        font.pixelSize: Theme.caption
                    }
                }

                ProgressLine {
                    visible: root.running
                    Layout.fillWidth: true
                    value: root.downloads.barProgress
                }
            }
        }

        // What JDownloader already transfers is not AnimeHub's to pause: with only that left,
        // there is nothing to pause or resume here. As the page's and the tray's: every download.
        Button {
            id: action
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: root.mode === DownloadsView.Failed ? root.downloads.canRetryFailed
                     : root.downloads.canPauseAll || root.downloads.canResumeAll
            flat: true
            text: root.mode === DownloadsView.Failed ? "Reintentar"
                  : root.downloads.canPauseAll ? "Pausar todo" : "Reanudar todo"
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
