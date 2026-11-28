import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Ultra-slim 36 px bottom download status bar (Steam / VS Code style) shown on every page
// except Descargas while any download is unfinished, paused, or failed. A full-bleed 2 px
// progress hairline runs across its top edge; clicking or pressing Enter/Space opens Descargas.
Item {
    id: root

    required property DownloadsView downloads

    signal clicked()

    readonly property string barState: downloads.barState
    readonly property bool active: barState === "active"
    readonly property bool paused: barState === "paused"
    readonly property bool failed: barState === "error"
    readonly property bool indeterminate: active && downloads.barProgress <= 0
    readonly property color barAccent: failed ? Theme.danger
                                     : paused ? Theme.warning
                                     : Theme.accent
    readonly property string statsText: {
        const speed = downloads.barSpeedText
        const pct = downloads.barPercentText
        if (speed !== "" && pct !== "")
            return speed + " · " + pct
        if (speed !== "")
            return speed
        if (pct !== "")
            return pct
        if (failed && downloads.canRetryFailed)
            return "Reintentar disponible"
        return ""
    }

    implicitHeight: 36

    HoverHandler { id: barHover }

    // Background click target kept as a sibling of the inline transport ToolButton (zero nested buttons).
    AbstractButton {
        id: barButton
        anchors.fill: parent
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        Keys.onReturnPressed: event => {
            event.accepted = true
            barButton.click()
        }
        Keys.onEnterPressed: event => {
            event.accepted = true
            barButton.click()
        }
        Accessible.role: Accessible.Button
        Accessible.name: "Descargas: " + root.downloads.barText
                         + (root.statsText !== "" ? ", " + root.statsText : "")
                         + (root.downloads.barProblem !== "" ? ", " + root.downloads.barProblem : "")
                         + (root.downloads.barNotice !== "" ? ", " + root.downloads.barNotice : "")
        Accessible.onPressAction: barButton.click()
        onClicked: root.clicked()

        background: Rectangle {
            color: barButton.down ? Theme.raised
                   : (barButton.hovered || barHover.hovered ? Theme.hover : Theme.pane)
            Behavior on color { ColorAnimation { duration: 110 } }

            // Full-bleed 2 px top progress hairline.
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 2
                color: Theme.stroke

                Rectangle {
                    id: progressFill
                    property real pulseOpacity: 0.85

                    height: parent.height
                    width: root.active
                           ? (root.indeterminate ? parent.width
                                                 : parent.width * Math.min(1, Math.max(0, root.downloads.barProgress)))
                           : (root.downloads.barProgress > 0
                              ? parent.width * Math.min(1, root.downloads.barProgress)
                              : parent.width)
                    color: root.barAccent
                    opacity: root.indeterminate ? pulseOpacity : (root.active ? 1.0 : 0.85)

                    SequentialAnimation on pulseOpacity {
                        running: root.visible && root.indeterminate
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.35; to: 0.95; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.95; to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                    }

                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
            }

            // Inset target keeps all four edges of FocusFrame visible inside the window border.
            Item {
                id: focusTarget
                anchors.fill: parent
                anchors.margins: 3
            }

            FocusFrame {
                target: focusTarget
                visible: barButton.activeFocus && InputModality.keyboard
                radiusInside: 0
            }
        }

        contentItem: Item {}
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 10

        Glyph {
            glyph: root.failed ? Theme.icon.warning
                   : root.paused ? Theme.icon.pause
                   : Theme.icon.download
            size: 13
            color: root.barAccent
        }

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.downloads.barText
            color: Theme.ink
            font.pixelSize: Theme.caption
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Accessible.ignored: true
            HoverHandler { id: textHover }
            ToolTip.visible: textHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
        }

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: Math.min(Math.ceil(implicitWidth),
                                          Math.max(120, Math.round(root.width * 0.30)))
            visible: text !== ""
            text: root.downloads.barProblem
            color: Theme.danger
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
            Accessible.ignored: true
            HoverHandler { id: problemHover }
            ToolTip.visible: problemHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
        }

        // MEGA quota notice when held back.
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: Math.min(320, Math.ceil(implicitWidth),
                                          Math.max(120, Math.round(root.width * 0.32)))
            visible: text !== ""
            text: root.downloads.barNotice
            color: Theme.warning
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
            Accessible.ignored: true
            HoverHandler { id: noticeHover }
            ToolTip.visible: noticeHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
        }

        Label {
            visible: text !== ""
            text: root.statsText
            color: root.failed ? Theme.danger : Theme.muted
            font.pixelSize: Theme.caption
            font.features: ({ "tnum": 1 })
            Accessible.ignored: true
        }

        // What JDownloader already transfers is not AnimeHub's to pause: hidden when
        // nothing can be paused, resumed, or retried from here.
        ToolButton {
            id: transportButton
            readonly property bool isRetry: root.failed && root.downloads.canRetryFailed
            readonly property bool isToggle: (root.active || root.paused)
                                             && (root.downloads.canPauseAll || root.downloads.canResumeAll)
            visible: isRetry || isToggle
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            padding: 0
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            display: AbstractButton.IconOnly
            text: isRetry ? "Reintentar fallidos"
                  : root.downloads.canPauseAll ? "Pausar todo" : "Reanudar todo"
            icon.source: Theme.source(isRetry ? Theme.icon.retry
                                      : root.downloads.canPauseAll ? Theme.icon.pause
                                                                   : Theme.icon.download)
            icon.width: 14
            icon.height: 14
            icon.color: isRetry ? Theme.danger : Theme.ink
            ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
            Accessible.name: text
            Accessible.onPressAction: transportButton.click()
            Keys.onReturnPressed: event => {
                event.accepted = true
                transportButton.click()
            }
            Keys.onEnterPressed: event => {
                event.accepted = true
                transportButton.click()
            }
            onClicked: {
                if (isRetry)
                    root.downloads.retryAllFailed()
                else if (root.downloads.canPauseAll)
                    root.downloads.pauseAll()
                else
                    root.downloads.resumeAll()
            }
        }

        Glyph {
            glyph: Theme.icon.chevronRight
            size: 11
            color: barButton.hovered || barHover.hovered ? Theme.ink : Theme.muted
        }
    }
}
