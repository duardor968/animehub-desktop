import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A series row on the Downloads page ("También descargando", "A continuación" or "Completadas").
// Unfolds into its episodes using the configured downloadEpisodesStyle:
//   - "cards" (default, Ronda 3 B): elevated Theme.raised panel + 2 px #2f81f7 left bar + 16:9 stills
//   - "compact" (Ronda 3 C): dense 36 px 1-line table with column header
// In "También descargando" (activeSection = true), shows an explicit neutral secondary
// "Episodios (N) ▾ / Ocultar ▴" button alongside live progress and speed.
Item {
    id: root

    required property int index
    required property string slug
    required property string title
    required property url poster
    required property string backdrop
    required property string summary
    required property real progress
    required property int failed
    required property string failedText
    required property bool canPause
    required property bool canResume
    required property bool canRetry
    required property bool canRetryNow
    required property string engineLine
    required property bool first
    required property string cancelText
    required property int filesOnDisk
    required property string diskText
    required property string watchText
    required property bool hasFolder
    required property string notice
    required property bool running
    required property int activeCount
    required property string runningSpeedText
    required property int cancelCount
    required property bool filesGone
    required property var tasks
    required property int taskCount

    required property DownloadsView downloads
    // Parallel downloading row under "También descargando".
    property bool activeSection: false
    // In line ("También descargando" or "A continuación"); otherwise in "Completadas".
    property bool queued: true
    // Only "A continuación" sorts by dragging.
    property bool draggable: queued && !activeSection
    property bool expanded: false
    property bool compactEpisodes: false
    property int barWidth: 200
    // How far it steps aside while another row is dragged over it.
    property real displacement: 0
    readonly property int actionsWidth: 84
    // The chevron's column, past the actions.
    readonly property int chevronRoom: 24
    readonly property bool narrow: width < 920
    // Narrow windows: actions keep their icon and say their name in a tooltip.
    readonly property bool compact: barWidth < 160
    readonly property string activeSummaryLine: root.runningSpeedText !== ""
                                                ? (root.runningSpeedText + " · " + root.summary)
                                                : root.summary

    signal toggled()
    signal menuRequested(Item anchor)
    signal episodeCancelRequested(var taskId, string label, string partial)
    signal moveRequested(int step)
    signal dragFinished()

    readonly property bool dragging: drag.active
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

    Item {
        id: header
        width: parent.width
        height: root.activeSection ? Math.max(96, activeRowLayout.implicitHeight + 20) : 84

        HoverHandler { id: rowHover }

        // Row fold/unfold trigger kept as a sibling of every inline action button (zero nested buttons).
        AbstractButton {
            id: headerTrigger
            anchors.fill: parent
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: root.title + ", " + (root.activeSection ? root.activeSummaryLine : root.summary)
                             + (root.failed > 0 ? ", " + root.failedText : "")
                             + (root.notice !== "" ? ", " + root.notice : "")
                             + (root.engineLine !== "" ? ", " + root.engineLine : "")
            Accessible.checkable: true
            Accessible.checked: root.expanded
            Accessible.onPressAction: headerTrigger.click()
            Keys.onReturnPressed: event => {
                event.accepted = true
                headerTrigger.click()
            }
            Keys.onEnterPressed: event => {
                event.accepted = true
                headerTrigger.click()
            }
            onClicked: root.toggled()

            Keys.onPressed: event => {
                if (!root.draggable || !(event.modifiers & Qt.AltModifier))
                    return
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    root.moveRequested(event.key === Qt.Key_Up ? -1 : 1)
                    event.accepted = true
                }
            }

            DragHandler {
                id: drag
                enabled: root.draggable
                target: null
                xAxis.enabled: false
                grabPermissions: PointerHandler.CanTakeOverFromAnything | PointerHandler.TakeOverForbidden
                cursorShape: Qt.ClosedHandCursor
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
                color: root.dragging ? Theme.raised
                       : (headerTrigger.hovered || rowHover.hovered) ? Theme.hover : "transparent"
                border.width: root.dragging ? 1 : 0
                border.color: Theme.stroke

                FocusFrame {
                    visible: headerTrigger.activeFocus && InputModality.keyboard
                    radiusInside: Theme.radius
                }
            }

            contentItem: Item {}
        }

        // ACTIVE PARALLEL ANIME LAYOUT ("También descargando"):
        // visually stronger than queued/completed rows below (54×76 poster + 18 px title)
        // with inline progress bar + explicit neutral "Episodios (N) ▾ / Ocultar ▴" button.
        RowLayout {
            id: activeRowLayout
            visible: root.activeSection
            anchors.fill: parent
            anchors.leftMargin: 32
            anchors.rightMargin: 32
            spacing: 16

            Poster {
                Layout.preferredWidth: 54
                Layout.preferredHeight: 76
                Layout.alignment: Qt.AlignVCenter
                source: root.poster
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)
                Accessible.ignored: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.ink
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: Math.ceil(implicitWidth)
                        text: root.activeSummaryLine
                        color: Theme.muted
                        font.pixelSize: Theme.body
                        elide: Text.ElideRight
                    }

                    Label {
                        visible: root.failed > 0
                        text: "· " + root.failedText
                        color: Theme.danger
                        font.pixelSize: Theme.body
                    }

                    ToolButton {
                        id: activeRetryButton
                        visible: root.canRetry
                        display: root.narrow ? AbstractButton.IconOnly : AbstractButton.TextOnly
                        text: "Reintentar fallidos"
                        font.pixelSize: Theme.caption
                        icon.source: Theme.source(Theme.icon.retry)
                        topPadding: 2
                        bottomPadding: 2
                        ToolTip.visible: (hovered || (activeFocus && InputModality.keyboard)) && root.narrow
                        ToolTip.delay: Theme.tooltipDelay
                        ToolTip.timeout: Theme.tooltipTimeout
                        ToolTip.text: text
                        Accessible.name: "Reintentar episodios fallidos de " + root.title
                        Accessible.onPressAction: activeRetryButton.click()
                        Keys.onReturnPressed: event => {
                            event.accepted = true
                            activeRetryButton.click()
                        }
                        Keys.onEnterPressed: event => {
                            event.accepted = true
                            activeRetryButton.click()
                        }
                        onClicked: root.downloads.retryFailed(root.slug)
                    }

                    Item { Layout.fillWidth: true }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.notice !== ""
                    text: root.notice
                    color: Theme.warning
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                    ToolTip.visible: activeNoticeHover.hovered && truncated
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    HoverHandler { id: activeNoticeHover }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.engineLine !== ""
                    text: root.engineLine
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideMiddle
                    ToolTip.visible: activeEngineHover.hovered && truncated
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    HoverHandler { id: activeEngineHover }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 400
                    from: 0
                    to: 1
                    value: root.progress
                    indeterminate: root.running && root.progress === 0 && width > 0
                    Accessible.name: root.activeSummaryLine
                }
            }

            // Strictly NEUTRAL secondary Fluent button in both collapsed and expanded states.
            Button {
                id: activeExpandButton
                visible: root.taskCount > 0
                Layout.alignment: Qt.AlignVCenter
                highlighted: false
                text: root.expanded ? "Ocultar" : ("Episodios (" + root.taskCount + ")")
                topPadding: 5
                bottomPadding: 5
                leftPadding: 11
                rightPadding: 9
                Accessible.name: (root.expanded ? "Ocultar episodios de " : "Ver episodios de ")
                                 + root.title + " (" + root.taskCount + ")"
                Accessible.checkable: true
                Accessible.checked: root.expanded
                Accessible.onPressAction: activeExpandButton.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    activeExpandButton.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    activeExpandButton.click()
                }
                onClicked: root.toggled()

                contentItem: RowLayout {
                    spacing: 5

                    Label {
                        text: activeExpandButton.text
                        color: Theme.ink
                        font.pixelSize: Theme.caption
                        font.weight: Font.Medium
                        Accessible.ignored: true
                    }

                    Glyph {
                        glyph: root.expanded ? Theme.icon.chevronUp : Theme.icon.chevronDown
                        size: 10
                        color: Theme.muted
                    }
                }
            }

            ToolButton {
                id: activeTransport
                readonly property string kind: root.running && root.canPause ? "pause"
                                               : root.canResume ? "resume"
                                               : root.canRetryNow ? "retry" : ""
                Layout.alignment: Qt.AlignVCenter
                visible: kind !== ""
                display: AbstractButton.IconOnly
                icon.source: Theme.source(kind === "pause" ? Theme.icon.pause
                                          : kind === "resume" ? Theme.icon.download
                                          : Theme.icon.retry)
                icon.width: 14
                icon.height: 14
                text: kind === "pause" ? "Pausar"
                      : kind === "resume" ? "Reanudar"
                      : "Reintentar ahora"
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: text + " " + root.title
                Accessible.onPressAction: activeTransport.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    activeTransport.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    activeTransport.click()
                }
                onClicked: {
                    if (kind === "pause")
                        root.downloads.pauseSeries(root.slug)
                    else if (kind === "resume")
                        root.downloads.resumeSeries(root.slug)
                    else
                        root.downloads.retryWaiting(root.slug)
                }
            }

            ToolButton {
                id: activeMore
                Layout.alignment: Qt.AlignVCenter
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                text: "Más opciones"
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Accessible.name: "Más opciones de " + root.title
                Accessible.onPressAction: activeMore.click()
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    activeMore.click()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    activeMore.click()
                }
                onClicked: root.menuRequested(activeMore)
            }
        }

        // QUEUED ("A continuación") & COMPLETED ("Completadas") ROW LAYOUT:
        // deliberately calmer than active parallel rows above (48×68 poster + 15 px subheading).
        RowLayout {
            visible: !root.activeSection
            anchors.fill: parent
            spacing: 0

            Poster {
                Layout.leftMargin: 32
                Layout.preferredWidth: 48
                Layout.preferredHeight: 68
                Layout.alignment: Qt.AlignVCenter
                source: root.poster
                Accessible.ignored: true
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
                    font.pixelSize: Theme.subheading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

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
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                        }

                        Label {
                            id: separator
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.failed > 0
                            text: " · "
                            color: Theme.muted
                            font.pixelSize: Theme.small
                        }

                        Label {
                            id: failures
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.failed > 0
                            text: root.failedText
                            color: Theme.danger
                            font.pixelSize: Theme.small
                        }

                        Item {
                            visible: retry.visible
                            width: 4
                            height: 1
                        }

                        ToolButton {
                            id: retry
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.canRetry
                            display: root.compact ? AbstractButton.IconOnly
                                                  : AbstractButton.TextOnly
                            text: "Reintentar fallidos"
                            font.pixelSize: Theme.small
                            icon.source: Theme.source(Theme.icon.retry)
                            ToolTip.visible: (hovered || (activeFocus && InputModality.keyboard)) && root.compact
                            ToolTip.delay: Theme.tooltipDelay
                            ToolTip.timeout: Theme.tooltipTimeout
                            ToolTip.text: text
                            Accessible.name: "Reintentar episodios fallidos de " + root.title
                            Accessible.onPressAction: retry.click()
                            Keys.onReturnPressed: event => {
                                event.accepted = true
                                retry.click()
                            }
                            Keys.onEnterPressed: event => {
                                event.accepted = true
                                retry.click()
                            }
                            onClicked: root.downloads.retryFailed(root.slug)
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.notice
                    color: Theme.warning
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                    ToolTip.visible: noticeHover.hovered && truncated
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    HoverHandler { id: noticeHover }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.engineLine !== ""
                    text: root.engineLine
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideMiddle
                    ToolTip.visible: engineHover.hovered && truncated
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    HoverHandler { id: engineHover }
                }
            }

            // Fixed columns so bars and actions align across rows.
            Item {
                Layout.preferredWidth: root.barWidth
                Layout.minimumWidth: root.barWidth
                Layout.leftMargin: 24
                Layout.fillHeight: true

                ProgressLine {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    implicitHeight: 4
                    visible: root.queued && !(root.running && root.progress === 0)
                    value: root.progress
                }

                ProgressBar {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    visible: root.queued && root.running && root.progress === 0 && width > 0
                    indeterminate: true
                    Accessible.name: root.summary
                }

                Button {
                    id: completedPrimaryButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.queued && (root.filesGone || root.watchText !== "") && !root.compact
                    text: root.filesGone ? "Volver a descargar" : root.watchText
                    icon.source: Theme.source(root.filesGone ? Theme.icon.download : Theme.icon.play)
                    Accessible.name: text + " — " + root.title
                    Accessible.onPressAction: completedPrimaryButton.click()
                    Keys.onReturnPressed: event => {
                        event.accepted = true
                        completedPrimaryButton.click()
                    }
                    Keys.onEnterPressed: event => {
                        event.accepted = true
                        completedPrimaryButton.click()
                    }
                    onClicked: root.filesGone ? root.downloads.downloadAgain(root.slug)
                                              : root.downloads.watchNext(root.slug)
                }

                ToolButton {
                    id: completedCompactButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.queued && (root.filesGone || root.watchText !== "") && root.compact
                    display: AbstractButton.IconOnly
                    text: root.filesGone ? "Volver a descargar" : root.watchText
                    icon.source: Theme.source(root.filesGone ? Theme.icon.download : Theme.icon.play)
                    ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    Accessible.name: text + " — " + root.title
                    Accessible.onPressAction: completedCompactButton.click()
                    Keys.onReturnPressed: event => {
                        event.accepted = true
                        completedCompactButton.click()
                    }
                    Keys.onEnterPressed: event => {
                        event.accepted = true
                        completedCompactButton.click()
                    }
                    onClicked: root.filesGone ? root.downloads.downloadAgain(root.slug)
                                              : root.downloads.watchNext(root.slug)
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
                    ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    Accessible.name: "Más opciones de " + root.title
                    Accessible.onPressAction: more.click()
                    Keys.onReturnPressed: event => {
                        event.accepted = true
                        more.click()
                    }
                    Keys.onEnterPressed: event => {
                        event.accepted = true
                        more.click()
                    }
                    onClicked: root.menuRequested(more)
                }

                ToolButton {
                    id: rowActionButton
                    readonly property string kind: !root.queued
                                                   ? (root.filesGone ? "forget" : root.hasFolder ? "folder" : "")
                                                   : root.running && root.canPause ? "pause"
                                                   : root.canResume ? "resume"
                                                   : root.canRetryNow ? "retry"
                                                   : !root.first ? "now" : ""
                    visible: kind !== ""
                    icon.source: Theme.source(kind === "pause" ? Theme.icon.pause
                                              : kind === "resume" ? Theme.icon.download
                                              : kind === "retry" ? Theme.icon.retry
                                              : kind === "now" ? Theme.icon.download
                                              : kind === "forget" ? Theme.icon.cancel
                                              : Theme.icon.folder)
                    display: AbstractButton.IconOnly
                    text: kind === "pause" ? "Pausar" : kind === "resume" ? "Reanudar"
                          : kind === "retry" ? "Reintentar ahora"
                          : kind === "now" ? "Descargar ahora" : kind === "forget" ? "Quitar de la lista"
                          : "Abrir la carpeta"
                    ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: text
                    Accessible.name: text + " — " + root.title
                    Accessible.onPressAction: rowActionButton.click()
                    Keys.onReturnPressed: event => {
                        event.accepted = true
                        rowActionButton.click()
                    }
                    Keys.onEnterPressed: event => {
                        event.accepted = true
                        rowActionButton.click()
                    }
                    onClicked: {
                        if (kind === "pause")
                            root.downloads.pauseSeries(root.slug)
                        else if (kind === "resume")
                            root.downloads.resumeSeries(root.slug)
                        else if (kind === "retry")
                            root.downloads.retryWaiting(root.slug)
                        else if (kind === "now")
                            root.downloads.prioritize(root.slug)
                        else if (kind === "forget")
                            root.downloads.forgetSeries(root.slug)
                        else
                            root.downloads.openSeriesFolder(root.slug)
                    }
                }
            }

            Glyph {
                Layout.preferredWidth: root.chevronRoom - 8
                Layout.leftMargin: 8
                Layout.rightMargin: 32
                glyph: root.expanded ? Theme.icon.chevronUp : Theme.icon.chevronDown
                size: 12
                color: Theme.muted
            }
        }
    }

    // Configurable expanded episode list: "cards" (Ronda 3 B) or "compact" (Ronda 3 C).
    Item {
        id: episodesBox
        anchors.top: header.bottom
        width: parent.width
        height: root.expanded ? expansionWrap.implicitHeight : 0
        visible: height > 0
        clip: true
        opacity: root.expanded ? 1 : 0
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Item {
            id: expansionWrap
            width: parent.width
            implicitHeight: root.compactEpisodes ? (compactPanel.implicitHeight + 12)
                                                 : (cardsPanel.implicitHeight + 14)

            // OPTION B ("cards", default): Elevated Theme.raised panel + 2 px #2f81f7 vertical left guide.
            Rectangle {
                id: cardsPanel
                visible: !root.compactEpisodes
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 32
                anchors.rightMargin: 32
                anchors.topMargin: 4
                implicitHeight: cardsColumn.implicitHeight + 12
                height: implicitHeight
                radius: Theme.radius
                color: Theme.raised
                border.width: 1
                border.color: Theme.stroke
                clip: true

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 1
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    width: 2
                    radius: 1
                    color: Theme.accent
                }

                Column {
                    id: cardsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 6

                    Repeater {
                        model: !root.compactEpisodes && (root.expanded || episodesBox.height > 0)
                               ? root.tasks : null

                        EpisodeLine {
                            width: cardsColumn.width
                            downloads: root.downloads
                            compactMode: false
                            narrow: root.narrow
                            showDivider: index < root.taskCount - 1
                            onCancelRequested: (taskId, label, partial) =>
                                root.episodeCancelRequested(taskId, label, partial)
                        }
                    }
                }
            }

            // OPTION C ("compact"): Dense 36 px 1-line table with column header.
            Column {
                id: compactPanel
                visible: root.compactEpisodes
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 32
                anchors.rightMargin: 32
                anchors.topMargin: 2

                Item {
                    width: parent.width
                    height: 24

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: root.narrow ? 10 : 12

                        Label {
                            Layout.fillWidth: false
                            Layout.preferredWidth: 68
                            Layout.minimumWidth: 68
                            Layout.maximumWidth: 68
                            text: "EP."
                            color: Theme.faint
                            font.pixelSize: Theme.micro
                            font.weight: Font.Bold
                        }
                        Label {
                            Layout.fillWidth: false
                            Layout.preferredWidth: root.narrow ? 132 : 210
                            Layout.minimumWidth: root.narrow ? 132 : 210
                            Layout.maximumWidth: root.narrow ? 132 : 210
                            text: "PROGRESO"
                            color: Theme.faint
                            font.pixelSize: Theme.micro
                            font.weight: Font.Bold
                        }
                        Label {
                            Layout.fillWidth: false
                            Layout.preferredWidth: root.narrow ? 92 : 120
                            Layout.minimumWidth: root.narrow ? 92 : 120
                            Layout.maximumWidth: root.narrow ? 92 : 120
                            text: "ESTADO"
                            color: Theme.faint
                            font.pixelSize: Theme.micro
                            font.weight: Font.Bold
                        }
                        Label {
                            visible: !root.narrow
                            Layout.fillWidth: false
                            Layout.preferredWidth: 96
                            Layout.minimumWidth: 96
                            Layout.maximumWidth: 96
                            text: "VELOCIDAD"
                            color: Theme.faint
                            font.pixelSize: Theme.micro
                            font.weight: Font.Bold
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: "TAMAÑO · RESTANTE"
                            color: Theme.faint
                            font.pixelSize: Theme.micro
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Item {
                            Layout.preferredWidth: 112
                            Layout.minimumWidth: 112
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.stroke
                    opacity: 0.7
                }

                Repeater {
                    model: root.compactEpisodes && (root.expanded || episodesBox.height > 0)
                           ? root.tasks : null

                    EpisodeLine {
                        width: compactPanel.width
                        downloads: root.downloads
                        compactMode: true
                        narrow: root.narrow
                        showDivider: index < root.taskCount - 1
                        onCancelRequested: (taskId, label, partial) =>
                            root.episodeCancelRequested(taskId, label, partial)
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.stroke
                    opacity: 0.75
                }
            }
        }
    }
}
