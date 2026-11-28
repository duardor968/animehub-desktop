import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// An episode inside an expanded series (Hero, También descargando, A continuación or Completadas).
// Supports two settings-driven presentations:
//   - compactMode = false ("cards", Ronda 3 B): 66 px rich row with 16:9 anime still and progress bar
//   - compactMode = true  ("compact", Ronda 3 C): 36 px 1-line table row aligned with CompactTableHeader
//     (EP. 68 | PROGRESO 132/210 | ESTADO 92/120 | VELOCIDAD 96 | TAMAÑO·RESTANTE fill | ACTIONS 112)
Item {
    id: root

    required property int index
    required property var taskId
    required property string episode
    required property string label
    required property string title
    required property url stillUrl
    required property string taskState
    required property string stateGroup
    required property string stateText
    required property real progress
    required property string percentText
    required property string speedText
    required property string sizeText
    required property string etaText
    required property string detail
    required property bool problem
    required property bool hasFile
    required property bool canPlay
    required property bool canPause
    required property bool canResume
    required property bool canRetry
    required property bool canRetryNow
    required property bool canCancel
    required property string partial

    required property DownloadsView downloads
    property bool compactMode: false
    property bool narrow: false
    property bool showDivider: false

    signal cancelRequested(var taskId, string label, string partial)

    readonly property bool completed: stateGroup === "completed"
    readonly property bool running: stateGroup === "running"
    readonly property bool paused: stateGroup === "paused"
    readonly property bool failed: stateGroup === "failed"

    readonly property string fullTitle: title !== "" ? (label + " · " + title) : label
    readonly property string cleanSize: sizeText.replace(" · En disco", "")
    readonly property bool showTransportAction: (canPause && running) || canResume || canRetry || canRetryNow
    // Keep unconfirmed JDownloader hand-overs (failed && !canRetry) protected from removal.
    readonly property bool showCancelAction: canCancel || (failed && canRetry)

    readonly property string cardsSubtitle: {
        if (failed)
            return detail !== "" ? detail : stateText
        if (completed) {
            if (!hasFile)
                return detail !== "" ? detail : "Archivo eliminado"
            return cleanSize !== "" && cleanSize !== "—" ? ("Completado · " + cleanSize) : "Completado"
        }
        if (running) {
            const parts = [stateText]
            if (percentText !== "" && percentText !== "—" && percentText !== stateText)
                parts.push(percentText)
            if (speedText !== "" && speedText !== "—")
                parts.push(speedText)
            if (sizeText !== "" && sizeText !== "—")
                parts.push(sizeText)
            if (etaText !== "")
                parts.push(etaText)
            return parts.join(" · ")
        }
        if (paused) {
            const parts = ["En pausa"]
            if (percentText !== "" && percentText !== "En pausa" && percentText !== "—")
                parts.push(percentText)
            if (sizeText !== "" && sizeText !== "—")
                parts.push(sizeText)
            return parts.join(" · ")
        }
        if (taskState === "waiting" && detail !== "")
            return detail
        return sizeText !== "" && sizeText !== "—" ? (stateText + " · " + sizeText) : stateText
    }

    readonly property color cardsSubtitleColor: failed ? Theme.danger
                                                : paused ? Theme.warning
                                                : running ? Theme.accentText
                                                : Theme.muted

    readonly property string compactSizeEta: {
        if (failed)
            return detail !== "" ? detail : "Error"
        if (taskState === "waiting" && detail !== "")
            return detail
        const parts = []
        if (sizeText !== "" && sizeText !== "—")
            parts.push(sizeText)
        if (etaText !== "")
            parts.push(etaText)
        return parts.length > 0 ? parts.join(" · ") : "—"
    }

    readonly property string transportIcon: canResume ? Theme.icon.download
                                            : (failed || canRetryNow) ? Theme.icon.retry
                                            : Theme.icon.pause
    readonly property string transportTooltip: failed ? "Reintentar"
                                               : canRetryNow ? "Reintentar ahora"
                                               : canResume ? "Reanudar"
                                               : "Pausar"
    readonly property string cancelTooltip: failed ? "Quitar de la lista" : "Cancelar la descarga"

    function triggerTransport() {
        if (failed || canRetryNow)
            downloads.retry(taskId)
        else if (canResume)
            downloads.resume(taskId)
        else
            downloads.pause(taskId)
    }

    function triggerCancel() {
        if (failed)
            downloads.forget(taskId)
        else
            cancelRequested(taskId, label, partial)
    }

    implicitHeight: compactMode ? 36 : 66
    Accessible.role: Accessible.ListItem
    Accessible.name: fullTitle + ", " + cardsSubtitle

    HoverHandler { id: rowHover }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: root.compactMode ? 0 : 4
        anchors.rightMargin: root.compactMode ? 0 : 4
        radius: Theme.radiusSm
        color: rowHover.hovered ? Theme.hoverSubtle : "transparent"
    }

    // CARDS MODE (Ronda 3 B): 16:9 anime still + title + status + progress bar + controls.
    RowLayout {
        visible: !root.compactMode
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 14

        Poster {
            Layout.preferredWidth: 92
            Layout.preferredHeight: 52
            Layout.alignment: Qt.AlignVCenter
            source: root.stillUrl
            opacity: root.completed ? 0.72 : 1.0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)
            Accessible.ignored: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 3

            Label {
                Layout.fillWidth: true
                text: root.fullTitle
                color: root.completed ? Theme.muted : Theme.ink
                font.pixelSize: Theme.small
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                ToolTip.visible: titleHover.hovered && truncated
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                HoverHandler { id: titleHover }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Glyph {
                    visible: root.completed && root.hasFile
                    glyph: Theme.icon.check
                    size: 11
                    color: Theme.success
                }

                Label {
                    Layout.fillWidth: true
                    text: root.cardsSubtitle
                    color: root.cardsSubtitleColor
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                    ToolTip.visible: subHover.hovered && truncated
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    HoverHandler { id: subHover }
                }
            }

            ProgressBar {
                visible: root.running || (root.paused && root.progress > 0)
                Layout.fillWidth: true
                Layout.maximumWidth: 280
                from: 0
                to: 1
                value: root.progress
                // Fluent times its indeterminate sweep by initial width; wait until laid out.
                indeterminate: root.running && root.progress === 0 && width > 0
                palette.active.accent: root.paused ? Theme.warning : Theme.accent
                palette.inactive.accent: root.paused ? Theme.warning : Theme.accent
                Accessible.name: root.cardsSubtitle
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 4

            ToolButton {
                id: cardPlayButton
                visible: root.completed && root.canPlay
                display: AbstractButton.TextBesideIcon
                text: "Ver"
                icon.source: Theme.source(Theme.icon.play)
                icon.width: 14
                icon.height: 14
                font.pixelSize: Theme.caption
                topPadding: 5
                bottomPadding: 5
                leftPadding: 10
                rightPadding: 10
                Accessible.name: "Ver " + root.label
                Accessible.onPressAction: cardPlayButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    cardPlayButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    cardPlayButton.click()
                }
                onClicked: root.downloads.play(root.taskId)
            }

            ToolButton {
                id: cardFolderButton
                visible: root.completed && root.hasFile
                display: AbstractButton.IconOnly
                text: "Mostrar en la carpeta"
                icon.source: Theme.source(Theme.icon.folder)
                icon.width: 14
                icon.height: 14
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: "Mostrar " + root.label + " en la carpeta"
                Accessible.onPressAction: cardFolderButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    cardFolderButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    cardFolderButton.click()
                }
                onClicked: root.downloads.showFile(root.taskId)
            }

            ToolButton {
                id: cardTransportButton
                visible: !root.completed && root.showTransportAction
                display: AbstractButton.IconOnly
                text: root.transportTooltip
                icon.source: Theme.source(root.transportIcon)
                icon.width: 14
                icon.height: 14
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: root.transportTooltip + " " + root.label
                Accessible.onPressAction: cardTransportButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    cardTransportButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    cardTransportButton.click()
                }
                onClicked: root.triggerTransport()
            }

            ToolButton {
                id: cardCancelButton
                visible: !root.completed && root.showCancelAction
                display: AbstractButton.IconOnly
                text: root.cancelTooltip
                icon.source: Theme.source(Theme.icon.cancel)
                icon.width: 14
                icon.height: 14
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: root.cancelTooltip + " (" + root.label + ")"
                Accessible.onPressAction: cardCancelButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    cardCancelButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    cardCancelButton.click()
                }
                onClicked: root.triggerCancel()
            }
        }
    }

    // COMPACT TABLE MODE (Ronda 3 C): Dense 36 px 1-line row without 16:9 thumbnails.
    RowLayout {
        visible: root.compactMode
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: root.narrow ? 10 : 12

        Label {
            Layout.fillWidth: false
            Layout.preferredWidth: 68
            Layout.minimumWidth: 68
            Layout.maximumWidth: 68
            text: "Ep. " + root.episode
            color: root.completed ? Theme.muted : Theme.ink
            font.pixelSize: Theme.caption
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            ToolTip.visible: epHover.hovered
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: root.fullTitle
            HoverHandler { id: epHover }
        }

        RowLayout {
            Layout.fillWidth: false
            Layout.preferredWidth: root.narrow ? 132 : 210
            Layout.minimumWidth: root.narrow ? 132 : 210
            Layout.maximumWidth: root.narrow ? 132 : 210
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                implicitHeight: 6

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 5
                    radius: 2
                    color: Theme.stroke
                    visible: !(root.running && root.progress === 0)

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, root.progress))
                        height: parent.height
                        radius: parent.radius
                        color: root.completed ? Theme.success
                               : root.failed ? Theme.danger
                               : root.paused ? Theme.warning
                               : Theme.accent
                    }
                }

                ProgressBar {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    visible: root.running && root.progress === 0 && width > 0
                    indeterminate: true
                    Accessible.name: root.cardsSubtitle
                }
            }

            Label {
                Layout.fillWidth: false
                Layout.preferredWidth: 38
                Layout.minimumWidth: 38
                Layout.maximumWidth: 38
                horizontalAlignment: Text.AlignRight
                text: root.percentText.indexOf("%") >= 0 ? root.percentText : "—"
                color: root.running ? Theme.accentText
                       : (root.completed && root.hasFile) ? Theme.success
                       : root.paused ? Theme.warning
                       : Theme.muted
                font.pixelSize: Theme.caption
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }

        Label {
            Layout.fillWidth: false
            Layout.preferredWidth: root.narrow ? 92 : 120
            Layout.minimumWidth: root.narrow ? 92 : 120
            Layout.maximumWidth: root.narrow ? 92 : 120
            text: root.stateText !== "" ? root.stateText : root.percentText
            color: root.running ? Theme.accentText
                   : (root.completed && root.hasFile) ? Theme.success
                   : root.paused ? Theme.warning
                   : root.failed ? Theme.danger
                   : Theme.muted
            font.pixelSize: Theme.caption
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Label {
            visible: !root.narrow
            Layout.fillWidth: false
            Layout.preferredWidth: 96
            Layout.minimumWidth: 96
            Layout.maximumWidth: 96
            text: root.running ? root.speedText : "—"
            color: root.running && root.speedText !== "—" ? Theme.ink : Theme.muted
            font.pixelSize: Theme.caption
            font.features: ({ "tnum": 1 })
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.compactSizeEta
            color: root.failed ? Theme.danger : Theme.muted
            font.pixelSize: Theme.caption
            elide: Text.ElideRight
            ToolTip.visible: sizeHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
            HoverHandler { id: sizeHover }
        }

        Row {
            Layout.preferredWidth: 112
            Layout.minimumWidth: 112
            Layout.alignment: Qt.AlignVCenter
            layoutDirection: Qt.RightToLeft
            spacing: 4

            ToolButton {
                id: compactFolderButton
                visible: root.completed && root.hasFile
                implicitHeight: 28
                topPadding: 3
                bottomPadding: 3
                leftPadding: 6
                rightPadding: 6
                display: AbstractButton.IconOnly
                text: "Mostrar en la carpeta"
                icon.source: Theme.source(Theme.icon.folder)
                icon.width: 13
                icon.height: 13
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: "Mostrar " + root.label + " en la carpeta"
                Accessible.onPressAction: compactFolderButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    compactFolderButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    compactFolderButton.click()
                }
                onClicked: root.downloads.showFile(root.taskId)
            }

            ToolButton {
                id: compactPlayButton
                visible: root.completed && root.canPlay
                implicitHeight: 28
                topPadding: 3
                bottomPadding: 3
                leftPadding: 8
                rightPadding: 8
                display: AbstractButton.TextBesideIcon
                text: "Ver"
                icon.source: Theme.source(Theme.icon.play)
                icon.width: 13
                icon.height: 13
                font.pixelSize: Theme.caption
                Accessible.name: "Ver " + root.label
                Accessible.onPressAction: compactPlayButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    compactPlayButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    compactPlayButton.click()
                }
                onClicked: root.downloads.play(root.taskId)
            }

            ToolButton {
                id: compactCancelButton
                visible: !root.completed && root.showCancelAction
                implicitHeight: 28
                topPadding: 3
                bottomPadding: 3
                leftPadding: 6
                rightPadding: 6
                display: AbstractButton.IconOnly
                text: root.cancelTooltip
                icon.source: Theme.source(Theme.icon.cancel)
                icon.width: 13
                icon.height: 13
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: root.cancelTooltip + " (" + root.label + ")"
                Accessible.onPressAction: compactCancelButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    compactCancelButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    compactCancelButton.click()
                }
                onClicked: root.triggerCancel()
            }

            ToolButton {
                id: compactTransportButton
                visible: !root.completed && root.showTransportAction
                implicitHeight: 28
                topPadding: 3
                bottomPadding: 3
                leftPadding: 6
                rightPadding: 6
                display: AbstractButton.IconOnly
                text: root.transportTooltip
                icon.source: Theme.source(root.transportIcon)
                icon.width: 13
                icon.height: 13
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: root.transportTooltip + " " + root.label
                Accessible.onPressAction: compactTransportButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    compactTransportButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    compactTransportButton.click()
                }
                onClicked: root.triggerTransport()
            }
        }
    }

    Rectangle {
        visible: root.showDivider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.compactMode ? 0 : 12
        anchors.rightMargin: root.compactMode ? 0 : 12
        height: 1
        color: Theme.stroke
        opacity: 0.45
    }
}
