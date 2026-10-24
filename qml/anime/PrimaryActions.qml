import QtQuick
import QtQuick.Controls
import AnimeHub

// The main button: what a press does with the episode to watch next ("Ver ep. 9", "Descargar
// ep. 9", "Pausar · 45 %"…). Where that episode's download is goes in the bar's figures, and the
// series' other downloads in its ⋯; only "Descargar ep. 9" offers more beside it. Fluent has no
// SplitButton: as in WinUI, the action and its ▾ are two contiguous buttons.
Row {
    id: root

    required property AnimeView view
    // Episodes are being chosen: the selection's bar holds the blue action, and this one steps
    // back.
    property bool selecting: false

    signal chooseRange()
    signal chooseEpisodes()

    readonly property int action: view.primaryAction
    readonly property string chevron: "e70d"
    // One width for every state, the ▾ included: the figures beside it never move. Wide enough
    // for "Continuar ep. 100" and "Reintentar la descarga"; a longer text widens it.
    readonly property int span: 200
    // The one episode missing is the next one: the main button already gets it, nothing is left
    // to choose.
    readonly property bool onlyNextMissing: view.missingCount === 1
                                            && (action === AnimeView.Download || action === AnimeView.Paused
                                                || action === AnimeView.Failed)
    // What else there is to download, for a series. Beside "Descargar ep. 9" as its ▾; in the
    // page's ⋯ in any other state.
    readonly property bool offersMore: !view.single && view.missingCount > 0 && !onlyNextMissing
    readonly property bool offersSplit: action === AnimeView.Download && offersMore

    visible: action !== AnimeView.None
    spacing: 1

    Button {
        id: main

        readonly property bool transferring: root.action === AnimeView.Downloading
                                             || root.action === AnimeView.Paused

        // Fluent keeps a disabled accent button blue: only a button that can be pressed is.
        highlighted: !root.selecting && enabled
        enabled: root.view.primaryEnabled
        implicitWidth: Math.max(root.span - (split.visible ? split.implicitWidth + root.spacing : 0),
                                implicitContentWidth + leftPadding + rightPadding)
        text: root.view.primaryText
        // The same as the text: what a press does. Waiting (the next airing, a file being
        // checked), a clock.
        icon.source: Theme.source(!enabled ? Theme.icon.clock : ({
            [AnimeView.Play]: Theme.icon.play,
            [AnimeView.Rewatch]: Theme.icon.play,
            [AnimeView.Download]: Theme.icon.download,
            [AnimeView.Queued]: Theme.icon.pause,
            [AnimeView.Downloading]: Theme.icon.pause,
            // Play is for watching: resuming a download keeps the download glyph.
            [AnimeView.Paused]: Theme.icon.download,
            [AnimeView.Failed]: Theme.icon.retry
        })[root.action] ?? Theme.icon.play)
        ToolTip.visible: hovered && root.view.primaryHint !== ""
        ToolTip.text: root.view.primaryHint
        Accessible.description: root.view.primaryHint
        onClicked: root.view.primary()

        // How much of the file is here, along the button's foot.
        Rectangle {
            visible: main.transferring && root.view.primaryProgress > 0
            x: main.background.x + 6
            y: main.background.y + main.background.height - 5
            width: main.background.width - 12
            height: 2
            radius: 1
            color: main.highlighted ? Qt.rgba(1, 1, 1, 0.3) : Theme.stroke

            Rectangle {
                width: parent.width * root.view.primaryProgress
                height: parent.height
                radius: parent.radius
                color: main.highlighted ? Theme.accentInk : Theme.accent
                Behavior on width { NumberAnimation { duration: 250 } }
            }
        }
    }

    Button {
        id: split
        visible: root.offersSplit
        highlighted: !root.selecting
        height: main.height
        // As narrow as its chevron: Fluent's minimum width is for labelled buttons.
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        icon.source: Theme.source(root.chevron)
        display: AbstractButton.IconOnly
        text: "Elegir qué descargar"
        ToolTip.visible: hovered
        ToolTip.text: text
        onClicked: downloadMenu.show(main)
    }

    ActionMenu {
        id: downloadMenu

        entries: [missingEntry, unwatchedEntry, rangeEntry, chooseEntry]

        MenuEntry {
            id: missingEntry
            text: "Lo que falta · " + root.view.missingText
            onTriggered: root.view.downloadMissing()
        }
        MenuEntry {
            id: unwatchedEntry
            // Only when it is not all that is missing.
            property bool wanted: root.view.unwatchedMissingCount > 0
                                  && root.view.unwatchedMissingCount < root.view.missingCount
            text: "Sin ver · " + root.view.unwatchedMissingText
            onTriggered: root.view.downloadUnwatched()
        }
        MenuEntry {
            id: rangeEntry
            text: "Un tramo…"
            onTriggered: root.chooseRange()
        }
        MenuEntry {
            id: chooseEntry
            property bool wanted: !root.selecting
            text: "Elegir episodios"
            onTriggered: root.chooseEpisodes()
        }
    }
}
