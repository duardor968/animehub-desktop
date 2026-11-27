import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import AnimeHub

// Compact free-scrolling Steam-style Hero for the Downloads page (Ronda 2 C + Ronda 3):
// scrolls out of view inside DownloadsPage's Flickable, uses headerBackdrop or a GPU MultiEffect
// blur of headerPoster (with AmbientBackdrop fallback while loading) fading into Theme.canvas,
// keeps headerTitle free of any hover fill box, exposes a neutral secondary expand/collapse button
// for headerTasks, and hosts the live SpeedGraph + SpeedLimitPopover in both active and idle states.
Rectangle {
    id: root
    objectName: "downloadsHeader"

    required property DownloadsView downloads
    required property AppSettings settings
    property bool expanded: false

    signal toggled(string slug)
    signal animeRequested(string slug)
    // The hero series' menu, which also holds the page's own "Abrir la carpeta de descargas" entry.
    signal menuRequested(var row, Item anchor)
    signal folderRequested()

    readonly property int mode: downloads.headerMode
    readonly property bool idle: mode === DownloadsView.Idle
    readonly property bool busy: mode === DownloadsView.Active && downloads.headerProgress === 0
    readonly property bool wide: width >= 1280
    readonly property bool narrow: width < 940
    readonly property bool hasBackdrop: !idle && downloads.headerBackdrop !== ""
                                        && backdropImage.status !== Image.Error
    readonly property string limitButtonText: settings.nativeLimitEnabled
                                              ? "Límite: " + settings.speedLimitText
                                              : "Limitar velocidad"
    readonly property string summaryLine: downloads.headerActiveSummary !== ""
                                          ? downloads.headerActiveSummary
                                          : downloads.headerEpisode
    readonly property color summaryColor: mode === DownloadsView.Failed ? Theme.danger
                                          : mode === DownloadsView.Paused ? Theme.warning
                                          : Theme.muted

    onIdleChanged: {
        if (limitEditor.opened)
            limitEditor.close()
    }

    implicitHeight: idle ? 56 : Math.max(158, heroRow.implicitHeight + 36)
    Behavior on implicitHeight { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    color: idle ? Theme.pane : Theme.canvas
    clip: true

    // Subtle pill trigger for the speed limit popover, shared between the active telemetry block
    // and the idle header so an active speed cap is always visible and reachable.
    component LimitTrigger: AbstractButton {
        id: control

        readonly property bool active: root.settings.nativeLimitEnabled
        readonly property color ink: active ? Theme.limitLine
                                            : hovered ? Theme.ink
                                            : !root.idle ? Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.86)
                                            : Theme.muted

        text: root.limitButtonText
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        topPadding: 4
        bottomPadding: 4
        leftPadding: 8
        rightPadding: 7
        Accessible.role: Accessible.Button
        Accessible.name: active ? "Límite de velocidad: " + root.settings.speedLimitText
                                : "Limitar velocidad de descarga"
        Accessible.description: "Abrir control de límite de velocidad"
        Accessible.onPressAction: clicked()
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        ToolTip.visible: hovered && !limitEditor.opened
        ToolTip.text: active ? "Cambiar o quitar el límite de velocidad"
                             : "Limitar velocidad de descarga"

        background: Rectangle {
            radius: Theme.radius
            color: control.active
                   ? Qt.rgba(Theme.limitLine.r, Theme.limitLine.g, Theme.limitLine.b,
                             control.down ? 0.24 : control.hovered ? 0.18 : 0.12)
                   : control.down ? Theme.raised
                   : control.hovered ? Theme.hover
                   : !root.idle ? Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.56)
                   : "transparent"
            border.width: control.active ? 1 : 0
            border.color: Qt.rgba(Theme.limitLine.r, Theme.limitLine.g, Theme.limitLine.b,
                                  control.hovered || control.down ? 0.38 : 0.22)

            FocusFrame { visible: control.activeFocus && InputModality.keyboard }
        }

        contentItem: RowLayout {
            spacing: 5

            Label {
                text: control.text
                color: control.ink
                font.pixelSize: 12
                font.weight: control.active ? Font.DemiBold : Font.Medium
                Accessible.ignored: true
            }
            Glyph {
                glyph: limitEditor.opened && limitEditor.anchorButton === control
                       ? Theme.icon.collapseAll : Theme.icon.expandAll
                size: 10
                color: control.ink
            }
        }
    }

    // Layer 0 (Fallback while backdrop/poster loads, and under software renderers):
    // smooth dominant-color ambient glow into Theme.canvas.
    AmbientBackdrop {
        anchors.fill: parent
        visible: !root.idle
        backdropUrl: ""
        posterUrl: root.idle ? "" : root.downloads.headerPoster
        surfaceColor: Theme.canvas
        leftScrim: false
    }

    // Layer 1A: Real anime backdrop when available.
    Image {
        id: backdropImage
        anchors.fill: parent
        visible: !root.idle && root.downloads.headerBackdrop !== ""
        source: visible ? root.downloads.headerBackdrop : ""
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignVCenter
        asynchronous: true
        cache: true
        sourceSize.width: Math.min(1920, Math.max(640, Math.ceil(width * Screen.devicePixelRatio)))
        opacity: status === Image.Ready ? 0.56 : 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }

    // Layer 1B: GPU MultiEffect blur of the poster when the anime has no dedicated backdrop.
    Image {
        id: posterBlurSource
        anchors.fill: parent
        visible: false
        source: !root.idle && !root.hasBackdrop ? root.downloads.headerPoster : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        sourceSize.width: 240
        sourceSize.height: 340
    }

    MultiEffect {
        id: posterBlurEffect
        anchors.fill: parent
        visible: !root.idle && !root.hasBackdrop && posterBlurSource.status === Image.Ready
        source: posterBlurSource
        blurEnabled: true
        blur: 1.0
        blurMax: 64
        saturation: 0.15
        autoPaddingEnabled: false
        opacity: visible ? 0.34 : 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }

    // Layer 2: Clean dark readability gradients dissolving smoothly into Theme.canvas.
    Rectangle {
        anchors.fill: parent
        visible: !root.idle
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.28) }
            GradientStop { position: 0.62; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.68) }
            GradientStop { position: 1.0; color: Theme.canvas }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.idle
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.72) }
            GradientStop { position: 0.52; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.36) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.48) }
        }
    }

    // Idle state: compact single-row top bar with title, speed limit trigger, and folder menu.
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 8
        visible: root.idle

        Label {
            Layout.fillWidth: true
            text: root.downloads.headerTitle
            color: Theme.muted
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }

        LimitTrigger {
            id: idleLimitButton
            objectName: visible ? "nativeLimitButton" : ""
            visible: root.idle
            onClicked: limitEditor.toggleFor(idleLimitButton)
        }

        ToolButton {
            id: pageMore
            visible: root.idle
            icon.source: Theme.source(Theme.icon.more)
            display: AbstractButton.IconOnly
            text: "Más opciones de Descargas"
            ToolTip.visible: hovered
            ToolTip.text: "Más opciones"
            onClicked: pageMenu.show(pageMore, true)
        }
    }

    // Active / Paused / Waiting / Failed Hero: compact ~158-168 px horizontal composition.
    RowLayout {
        id: heroRow
        visible: !root.idle
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 18

        Poster {
            id: poster
            Layout.preferredWidth: 76
            Layout.preferredHeight: 108
            Layout.alignment: Qt.AlignVCenter
            visible: root.downloads.headerPoster.toString() !== ""
            source: root.downloads.headerPoster
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)
            Accessible.ignored: true

            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.animeRequested(root.downloads.headerSlug) }
        }

        ColumnLayout {
            id: subject
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 5

            // Clean interactive title: NO gray hover rectangle box; subtle click opens the anime.
            AbstractButton {
                id: titleButton
                Layout.minimumWidth: 0
                Layout.maximumWidth: subject.width
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Heading
                Accessible.name: root.downloads.headerTitle
                Accessible.description: "Abrir ficha del anime"
                Accessible.onPressAction: clicked()
                onClicked: root.animeRequested(root.downloads.headerSlug)

                HoverHandler { cursorShape: Qt.PointingHandCursor }

                background: Item {
                    FocusFrame {
                        visible: titleButton.activeFocus && InputModality.keyboard
                        radiusInside: 2
                    }
                }

                contentItem: Label {
                    id: titleLabel
                    text: root.downloads.headerTitle
                    color: titleButton.hovered ? "#ffffff" : Theme.ink
                    font.pixelSize: root.narrow ? 18 : 20
                    font.weight: Font.DemiBold
                    font.underline: titleButton.hovered
                    elide: Text.ElideRight
                    Accessible.ignored: true
                    ToolTip.visible: titleButton.hovered && truncated
                    ToolTip.text: text
                }
            }

            // Multi-episode or single-episode live summary.
            Label {
                Layout.fillWidth: true
                text: root.summaryLine
                color: root.summaryColor
                font.pixelSize: 13
                elide: Text.ElideRight
                ToolTip.visible: summaryHover.hovered && truncated
                ToolTip.text: text
                HoverHandler { id: summaryHover }
            }

            // JDownloader destination line and partial failure retry action (0 px when absent).
            Repeater {
                model: root.idle ? null : root.downloads.current

                ColumnLayout {
                    id: currentMeta

                    required property string slug
                    required property string engineLine
                    required property bool canRetry
                    required property int failed
                    required property string failedText

                    Layout.fillWidth: true
                    visible: engineLine !== "" || (failed > 0 && (canRetry || root.mode !== DownloadsView.Failed))
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        visible: currentMeta.engineLine !== ""
                        text: currentMeta.engineLine
                        color: Theme.muted
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        ToolTip.visible: engineHover.hovered && truncated
                        ToolTip.text: text
                        HoverHandler { id: engineHover }
                    }

                    RowLayout {
                        visible: currentMeta.failed > 0 && (currentMeta.canRetry || root.mode !== DownloadsView.Failed)
                        spacing: 8

                        Label {
                            visible: root.mode !== DownloadsView.Failed
                            text: currentMeta.failedText
                            color: Theme.danger
                            font.pixelSize: 12
                        }

                        Button {
                            visible: currentMeta.canRetry
                            text: currentMeta.failed === 1 ? "Reintentar" : "Reintentar fallidos"
                            topPadding: 4
                            bottomPadding: 4
                            onClicked: root.downloads.retryFailed(currentMeta.slug)
                        }
                    }
                }
            }

            // MEGA quota wait notice + immediate retry button (0 px when absent).
            RowLayout {
                Layout.fillWidth: true
                visible: root.downloads.headerNotice !== ""
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    text: root.downloads.headerNotice
                    color: Theme.warning
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    ToolTip.visible: noticeHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: noticeHover }
                }

                Button {
                    id: retryNow
                    flat: true
                    text: "Reintentar ahora"
                    topPadding: 3
                    bottomPadding: 3
                    onClicked: root.downloads.retryMegaQuota()
                }

                Item { Layout.fillWidth: true }
            }

            ProgressBar {
                Layout.fillWidth: true
                Layout.maximumWidth: 480
                Layout.topMargin: 2
                visible: root.mode !== DownloadsView.Failed || root.downloads.headerProgress > 0
                from: 0
                to: 1
                value: root.downloads.headerProgress
                indeterminate: root.busy && width > 0
                palette.active.accent: root.mode === DownloadsView.Failed ? Theme.danger
                                       : root.mode === DownloadsView.Paused ? Theme.warning
                                       : Theme.accent
                palette.inactive.accent: root.mode === DownloadsView.Failed ? Theme.danger
                                         : root.mode === DownloadsView.Paused ? Theme.warning
                                         : Theme.accent
                Accessible.name: root.summaryLine
            }

            // Hero actions: strictly NEUTRAL secondary Fluent expand/collapse toggle + Pause/Resume + menu.
            RowLayout {
                Layout.topMargin: 4
                spacing: 8

                Button {
                    id: heroExpandButton
                    objectName: "heroEpisodesToggle"
                    visible: root.downloads.headerTaskCount > 0
                    highlighted: false
                    text: root.expanded
                          ? "Ocultar episodios"
                          : ("Ver episodios (" + root.downloads.headerTaskCount + ")")
                    topPadding: 5
                    bottomPadding: 5
                    leftPadding: 12
                    rightPadding: 10
                    Accessible.checkable: true
                    Accessible.checked: root.expanded
                    onClicked: root.toggled(root.downloads.headerSlug)

                    contentItem: RowLayout {
                        spacing: 6

                        Label {
                            text: heroExpandButton.text
                            color: Theme.ink
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            Accessible.ignored: true
                        }

                        Glyph {
                            glyph: root.expanded ? Theme.icon.collapseAll : Theme.icon.expandAll
                            size: 10
                            color: Theme.muted
                        }
                    }
                }

                Button {
                    visible: root.downloads.canPauseAll || root.downloads.canResumeAll
                    highlighted: false
                    text: root.downloads.canPauseAll ? "Pausar todo" : "Reanudar todo"
                    icon.source: Theme.source(root.downloads.canPauseAll ? Theme.icon.pause
                                                                         : Theme.icon.play)
                    icon.width: 14
                    icon.height: 14
                    topPadding: 5
                    bottomPadding: 5
                    leftPadding: 12
                    rightPadding: 12
                    onClicked: root.downloads.canPauseAll ? root.downloads.pauseAll()
                                                          : root.downloads.resumeAll()
                }

                Repeater {
                    id: seriesActions
                    model: root.idle ? null : root.downloads.current

                    RowLayout {
                        id: actions

                        required property string slug
                        required property string title
                        required property bool first
                        required property bool hasFolder
                        required property string cancelText
                        required property int cancelCount
                        required property bool canPause
                        required property bool canRetryNow
                        required property bool running

                        spacing: 4

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

                ToolButton {
                    id: activeFallbackMore
                    visible: seriesActions.count === 0
                    icon.source: Theme.source(Theme.icon.more)
                    display: AbstractButton.IconOnly
                    text: "Más opciones de Descargas"
                    ToolTip.visible: hovered
                    ToolTip.text: "Más opciones"
                    onClicked: pageMenu.show(activeFallbackMore, true)
                }
            }
        }

        // Right telemetry block: C++ SpeedGraph + live speed + speed limit popover + session totals.
        ColumnLayout {
            id: telemetry
            Layout.preferredWidth: root.wide ? 292 : root.width >= 1000 ? 268 : 232
            Layout.minimumWidth: 224
            Layout.alignment: Qt.AlignVCenter
            spacing: 6

            SpeedGraph {
                id: graph
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                samples: root.downloads.speedSamples
                nativeSamples: root.downloads.nativeSpeedSamples
                limit: root.settings.nativeLimitEnabled ? root.settings.nativeLimitKBps * 1000 : 0

                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onTapped: limitEditor.toggleFor(activeLimitButton)
                }
                HoverHandler {
                    id: chartHover
                    cursorShape: Qt.PointingHandCursor
                }
                ToolTip.visible: chartHover.hovered && !limitEditor.opened
                ToolTip.text: "Cambiar límite de velocidad"
            }

            // Engine split legend only while JDownloader is actively contributing.
            RowLayout {
                Layout.fillWidth: true
                visible: root.downloads.jdActive
                spacing: 10

                Item { Layout.fillWidth: true }

                RowLayout {
                    spacing: 4

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 2
                        color: Theme.accent
                    }
                    Label {
                        text: "AnimeHub · " + root.downloads.nativeSpeedText
                        color: Theme.muted
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    spacing: 4

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 2
                        color: Theme.muted
                    }
                    Label {
                        text: "JD · " + root.downloads.jdSpeedText
                        color: Theme.muted
                        font.pixelSize: 11
                        Accessible.name: "JDownloader: " + root.downloads.jdSpeedText
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: root.downloads.active.count > 0
                          ? (root.downloads.totalSpeedText + " total")
                          : root.downloads.totalSpeedText
                    color: Theme.ink
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Accessible.name: "Velocidad actual: " + root.downloads.totalSpeedText
                }

                Item { Layout.fillWidth: true }

                LimitTrigger {
                    id: activeLimitButton
                    objectName: visible ? "nativeLimitButton" : ""
                    visible: !root.idle
                    onClicked: limitEditor.toggleFor(activeLimitButton)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "Máx. " + root.downloads.peakText
                    color: Theme.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Accessible.name: "Velocidad máxima: " + root.downloads.peakText
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "Sesión " + root.downloads.totalText
                    color: Theme.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Accessible.name: "Descargado en esta sesión: " + root.downloads.totalText
                }
            }
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

    Popup {
        id: limitEditor
        objectName: "nativeLimitPopup"

        property Item anchorButton: null

        function toggleFor(anchor) {
            if (opened) {
                close()
                return
            }
            if (!anchor || !parent)
                return
            anchorButton = anchor
            const popupW = Math.max(implicitWidth,
                                    speedControls.implicitWidth + leftPadding + rightPadding)
            const popupH = Math.max(implicitHeight,
                                    speedControls.implicitHeight + topPadding + bottomPadding)
            const pt = anchor.mapToItem(parent, anchor.width, anchor.height)
            x = Math.max(8, Math.min(parent.width - popupW - 8, pt.x - popupW))
            y = Math.max(8, Math.min(parent.height - popupH - 8, pt.y + 6))
            open()
        }

        parent: Overlay.overlay
        popupType: Popup.Item
        padding: 16
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onAboutToShow: {
            speedControls.resetFromSettings()
            Qt.callLater(() => {
                if (limitEditor.visible)
                    speedControls.focusToggle(InputModality.reason)
            })
        }
        onOpened: speedControls.focusToggle(InputModality.reason)
        onAboutToHide: {
            if (anchorButton && anchorButton.visible
                    && (limitEditor.activeFocus || InputModality.keyboard))
                anchorButton.forceActiveFocus(InputModality.reason)
        }

        contentItem: NativeSpeedLimit {
            id: speedControls
            settings: root.settings
            showApply: true
            onApplied: limitEditor.close()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
        opacity: 0.85
    }
}
