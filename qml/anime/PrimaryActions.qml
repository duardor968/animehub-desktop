import QtQuick
import QtQuick.Controls
import AnimeHub

// Dual-role primary controls for an anime page:
// - When 0 episodes are on disk (idle or downloading an individual episode), the primary accent
//   control stays global as a single-piece Fluent SplitButton ("Descargar todo (N) | ▾" or
//   "Descargar restantes (N) | ▾"), never mutating into a single-episode pause action.
// - When the next episode is on disk and others remain missing, primary plays/continues it while a
//   secondary neutral SplitButton offers the remaining batch (compacting to "↓ (N) | ▾" on narrow
//   windows so trailing toolbar icons never clip).
Row {
    id: root

    required property AnimeView view
    // Episodes are being chosen: the selection's bar holds the blue action, and this one steps
    // back.
    property bool selecting: false
    // Narrow ActionBar (< 760 px): compact the secondary batch SplitButton to "↓ (N) | ▾".
    property bool compact: false

    signal chooseRange()
    signal chooseEpisodes()

    readonly property int action: view.primaryAction
    readonly property string chevron: "e70d"
    readonly property bool mainIsAll: !view.single && action === AnimeView.DownloadAll
                                      && view.missingCount > 0
    readonly property bool offersAll: view.showSecondaryDownload

    // Not known yet (the anime or its episodes still load): the button holds its place, disabled,
    // so the bar does not change shape when it is. Waiting for the next airing is told by a
    // figure, not by a button that cannot be pressed; with no episodes there is nothing to press.
    readonly property bool pending: action === AnimeView.None
                                    && (!view.loaded || view.refreshing)
    visible: pending || (action !== AnimeView.None && action !== AnimeView.Upcoming)
    height: 36
    spacing: 8

    // Single-piece seamless Fluent SplitButton: left action half, 1 px internal divider, and
    // right 32 px chevron half inside one rounded surface with zero double-border seam.
    component SplitAction: Rectangle {
        id: split

        property bool highlighted: false
        property string text
        property string iconGlyph
        property string hint
        property string actionAccessibleName: text
        property string menuAccessibleName: "Elegir qué descargar"
        property real progress: 0
        property bool showProgress: false
        property int minActionWidth: 0

        signal activated()
        signal menuRequested()

        readonly property int surfaceInset: highlighted ? 0 : 1
        readonly property color contentColor: !enabled
                                              ? (highlighted ? Theme.muted : Theme.faint)
                                              : (highlighted ? Theme.accentInk : Theme.ink)
        readonly property color dividerColor: highlighted
                                              ? Qt.rgba(1, 1, 1, enabled ? 0.22 : 0.12)
                                              : Theme.stroke

        implicitWidth: splitRow.implicitWidth
        implicitHeight: 36
        height: 36
        radius: 4
        color: highlighted
               ? (enabled ? Theme.accent : Theme.accentDisabled)
               : (enabled ? Theme.raised : Qt.rgba(Theme.raised.r, Theme.raised.g, Theme.raised.b, 0.55))
        border.width: highlighted ? 0 : 1
        border.color: Theme.stroke

        Row {
            id: splitRow
            height: parent.height
            spacing: 0

            AbstractButton {
                id: actionBtn
                height: split.height
                leftPadding: root.compact && !split.highlighted ? 10 : 14
                rightPadding: root.compact && !split.highlighted ? 10 : 12
                topPadding: 0
                bottomPadding: 0
                implicitWidth: Math.max(split.minActionWidth,
                                        implicitContentWidth + leftPadding + rightPadding)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                text: split.text
                ToolTip.visible: hovered && split.hint !== ""
                ToolTip.text: split.hint
                Accessible.role: Accessible.Button
                Accessible.name: split.actionAccessibleName
                Accessible.description: split.hint
                onClicked: split.activated()
                Keys.onReturnPressed: event => { event.accepted = true; clicked() }
                Keys.onEnterPressed: event => { event.accepted = true; clicked() }
                Keys.onSpacePressed: event => { event.accepted = true; clicked() }

                background: Item {
                    anchors.fill: parent
                    anchors.leftMargin: split.surfaceInset
                    anchors.topMargin: split.surfaceInset
                    anchors.bottomMargin: split.surfaceInset
                    clip: true

                    Rectangle {
                        x: 0
                        y: 0
                        width: parent.width + split.radius
                        height: parent.height
                        radius: Math.max(0, split.radius - split.surfaceInset)
                        color: !actionBtn.enabled ? "transparent"
                             : split.highlighted
                               ? (actionBtn.down ? Qt.rgba(0, 0, 0, 0.16)
                                 : actionBtn.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
                               : (actionBtn.down ? Qt.rgba(0, 0, 0, 0.18)
                                 : actionBtn.hovered ? Theme.hover : "transparent")
                    }
                }

                contentItem: Item {
                    implicitWidth: actionRow.implicitWidth
                    implicitHeight: actionRow.implicitHeight

                    Row {
                        id: actionRow
                        anchors.centerIn: parent
                        spacing: root.compact && !split.highlighted ? 5 : 7

                        Glyph {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: split.iconGlyph !== ""
                            glyph: split.iconGlyph
                            size: 14
                            color: split.contentColor
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: split.text
                            color: split.contentColor
                            font.pixelSize: Theme.body
                            font.weight: split.highlighted ? Font.DemiBold : Font.Normal
                            Accessible.ignored: true
                        }
                    }
                }

                Rectangle {
                    visible: split.showProgress && split.progress > 0
                    x: 6
                    y: parent.height - 5
                    width: parent.width - 12
                    height: 2
                    radius: 1
                    color: split.highlighted ? Qt.rgba(1, 1, 1, 0.3) : Theme.stroke

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, split.progress))
                        height: parent.height
                        radius: parent.radius
                        color: split.highlighted ? Theme.accentInk : Theme.accent
                        Behavior on width { NumberAnimation { duration: 250 } }
                    }
                }

                FocusFrame {
                    visible: actionBtn.activeFocus && InputModality.keyboard
                    radiusInside: split.radius
                }
            }

            Rectangle {
                y: split.surfaceInset
                width: 1
                height: split.height - 2 * split.surfaceInset
                color: split.dividerColor
            }

            AbstractButton {
                id: chevronBtn
                width: 32
                implicitWidth: 32
                height: split.height
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                text: split.menuAccessibleName
                ToolTip.visible: hovered
                ToolTip.text: split.menuAccessibleName
                Accessible.role: Accessible.Button
                Accessible.name: split.menuAccessibleName
                onClicked: split.menuRequested()
                Keys.onReturnPressed: event => { event.accepted = true; clicked() }
                Keys.onEnterPressed: event => { event.accepted = true; clicked() }
                Keys.onSpacePressed: event => { event.accepted = true; clicked() }
                Keys.onDownPressed: event => { event.accepted = true; clicked() }

                background: Item {
                    anchors.fill: parent
                    anchors.rightMargin: split.surfaceInset
                    anchors.topMargin: split.surfaceInset
                    anchors.bottomMargin: split.surfaceInset
                    clip: true

                    Rectangle {
                        x: -split.radius
                        y: 0
                        width: parent.width + split.radius
                        height: parent.height
                        radius: Math.max(0, split.radius - split.surfaceInset)
                        color: !chevronBtn.enabled ? "transparent"
                             : split.highlighted
                               ? (chevronBtn.down ? Qt.rgba(0, 0, 0, 0.16)
                                 : chevronBtn.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
                               : (chevronBtn.down ? Qt.rgba(0, 0, 0, 0.18)
                                 : chevronBtn.hovered ? Theme.hover : "transparent")
                    }
                }

                contentItem: Glyph {
                    glyph: root.chevron
                    size: 12
                    color: split.contentColor
                }

                FocusFrame {
                    visible: chevronBtn.activeFocus && InputModality.keyboard
                    radiusInside: split.radius
                }
            }
        }
    }

    SplitAction {
        id: primarySplit
        visible: root.mainIsAll
        enabled: !root.pending && root.view.primaryEnabled
        highlighted: !root.selecting && enabled
        minActionWidth: root.compact ? 80 : 100
        text: root.view.primaryText
        iconGlyph: Theme.icon.download
        hint: root.view.primaryHint
        onActivated: root.view.primary()
        onMenuRequested: downloadMenu.show(primarySplit)
    }

    AccentButton {
        id: main

        readonly property bool transferring: root.action === AnimeView.Downloading
                                             || root.action === AnimeView.Paused
                                             || root.action === AnimeView.PauseAll
                                             || root.action === AnimeView.ResumeAll

        visible: !root.mainIsAll
        // Crisp #ffffff text & icon over #2f81f7; steps back to neutral while selecting episodes.
        emphasized: !root.selecting && enabled
        enabled: !root.pending && root.view.primaryEnabled
        implicitHeight: 36
        height: 36
        implicitWidth: Math.max(root.compact ? 112 : 132,
                                implicitContentWidth + leftPadding + rightPadding)
        text: root.pending ? "Cargando…" : root.view.primaryText
        // The same as the text: what a press does. Waiting (the next airing, a file being
        // checked), a clock.
        glyphCode: !enabled ? Theme.icon.clock : ({
            [AnimeView.Play]: Theme.icon.play,
            [AnimeView.Rewatch]: Theme.icon.play,
            [AnimeView.Download]: Theme.icon.download,
            [AnimeView.DownloadAll]: Theme.icon.download,
            [AnimeView.PauseAll]: Theme.icon.pause,
            [AnimeView.ResumeAll]: Theme.icon.download,
            [AnimeView.Queued]: Theme.icon.pause,
            [AnimeView.Downloading]: Theme.icon.pause,
            // Play is for watching: resuming a download keeps the download glyph.
            [AnimeView.Paused]: Theme.icon.download,
            [AnimeView.Failed]: Theme.icon.retry
        })[root.action] ?? Theme.icon.play
        iconSize: 14
        ToolTip.visible: hovered && root.view.primaryHint !== ""
        ToolTip.text: root.view.primaryHint
        Accessible.description: root.view.primaryHint
        onClicked: root.view.primary()
        Keys.onReturnPressed: event => { event.accepted = true; clicked() }
        Keys.onEnterPressed: event => { event.accepted = true; clicked() }
        Keys.onSpacePressed: event => { event.accepted = true; clicked() }

        // How much of the file is here, along the button's foot.
        Rectangle {
            visible: main.transferring && root.view.primaryProgress > 0
            x: 6
            y: main.height - 5
            width: main.width - 12
            height: 2
            radius: 1
            color: main.emphasized ? Qt.rgba(1, 1, 1, 0.3) : Theme.stroke

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root.view.primaryProgress))
                height: parent.height
                radius: parent.radius
                color: main.emphasized ? Theme.accentInk : Theme.accent
                Behavior on width { NumberAnimation { duration: 250 } }
            }
        }
    }

    // Secondary batch download SplitButton when primary plays/continues an episode on disk while
    // other episodes remain unqueued. Compacts to "↓ (N) | ▾" on narrow panes.
    SplitAction {
        id: secondarySplit
        visible: root.offersAll
        highlighted: false
        text: root.compact ? "(" + root.view.missingCount + ")" : root.view.batchDownloadText
        iconGlyph: Theme.icon.download
        actionAccessibleName: root.view.batchDownloadText
        hint: root.view.batchDownloadText + ": " + root.view.missingText
        onActivated: root.view.downloadMissing()
        onMenuRequested: downloadMenu.show(secondarySplit)
    }

    ActionMenu {
        id: downloadMenu

        entries: [unwatchedEntry, rangeEntry, chooseEntry,
                  activeSep, pauseEntry, resumeEntry, retryEntry, cancelEntry]

        MenuEntry {
            id: unwatchedEntry
            // Only when it is not all that is missing.
            property bool wanted: root.view.unwatchedMissingCount > 0
                                  && root.view.unwatchedMissingCount < root.view.missingCount
            text: "Descargar no vistos (" + root.view.unwatchedMissingCount + ")"
            onTriggered: root.view.downloadUnwatched()
        }
        MenuEntry {
            id: rangeEntry
            text: "Elegir un rango…"
            onTriggered: root.chooseRange()
        }
        MenuEntry {
            id: chooseEntry
            property bool wanted: !root.selecting
            text: "Elegir episodios…"
            onTriggered: root.chooseEpisodes()
        }
        MenuSeparator {
            id: activeSep
            property bool separator: true
        }
        MenuEntry {
            id: pauseEntry
            property bool wanted: root.view.downloadRunning && root.action !== AnimeView.PauseAll
            text: "Pausar descargas en curso"
            onTriggered: root.view.pauseSeries()
        }
        MenuEntry {
            id: resumeEntry
            property bool wanted: root.view.downloadPaused && root.action !== AnimeView.ResumeAll
            text: "Reanudar descargas pausadas"
            onTriggered: root.view.resumeSeries()
        }
        MenuEntry {
            id: retryEntry
            property bool wanted: root.view.failedCount > 0 && root.action !== AnimeView.Failed
            text: "Reintentar fallidos (" + root.view.failedCount + ")"
            onTriggered: root.view.retryFailed()
        }
        MenuEntry {
            id: cancelEntry
            property bool wanted: root.view.hasDownloads
            text: "Cancelar descargas en curso"
            onTriggered: root.view.cancelSeries(false)
        }
    }
}
