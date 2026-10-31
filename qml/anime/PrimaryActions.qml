import QtQuick
import QtQuick.Controls
import AnimeHub

// The main button: what a press does ("Ver ep. 9", "Descargar todo (13)", "Pausar la descarga
// (13)", "Pausar · 45 %"…). Where a download is goes in the bar's figures; only a download
// ("Descargar ep. 9", "Descargar todo") offers more beside it. Fluent has no SplitButton: as in
// WinUI, the action and its ▾ are two contiguous buttons.
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
    // for "Descargar todo (100)" with its ▾; a longer text widens it.
    readonly property int span: 224
    // The one episode missing is the next one: the main button already gets it, nothing is left
    // to choose.
    readonly property bool onlyNextMissing: view.missingCount === 1
                                            && (action === AnimeView.Download || action === AnimeView.Paused
                                                || action === AnimeView.Failed)
    // What else there is to download, for a series. Beside a download as its ▾; in the page's ⋯
    // in any other state.
    readonly property bool offersMore: !view.single && view.missingCount > 0 && !onlyNextMissing
    readonly property bool offersSplit: (action === AnimeView.Download || action === AnimeView.DownloadAll)
                                        && offersMore

    // Not known yet (the anime or its episodes still load): the button holds its place, disabled,
    // so the bar does not change shape when it is. Waiting for the next airing is told by a
    // figure, not by a button that cannot be pressed; with no episodes there is nothing to press.
    readonly property bool pending: action === AnimeView.None
                                    && (!view.loaded || view.refreshing)
    visible: pending || (action !== AnimeView.None && action !== AnimeView.Upcoming)
    spacing: 1

    Button {
        id: main

        readonly property bool transferring: root.action === AnimeView.Downloading
                                             || root.action === AnimeView.Paused
                                             || root.action === AnimeView.PauseAll
                                             || root.action === AnimeView.ResumeAll

        // Fluent keeps a disabled accent button blue: only a button that can be pressed is.
        highlighted: !root.selecting && enabled
        enabled: !root.pending && root.view.primaryEnabled
        implicitWidth: Math.max(root.span - (split.visible ? split.implicitWidth + root.spacing : 0),
                                implicitContentWidth + leftPadding + rightPadding)
        text: root.pending ? "Cargando…" : root.view.primaryText
        // The same as the text: what a press does. Waiting (the next airing, a file being
        // checked), a clock.
        icon.source: Theme.source(!enabled ? Theme.icon.clock : ({
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
            // "Descargar todo" already is this.
            property bool wanted: root.action !== AnimeView.DownloadAll
            text: "Todos los que faltan (" + root.view.missingCount + ")"
            onTriggered: root.view.downloadMissing()
        }
        MenuEntry {
            id: unwatchedEntry
            // Only when it is not all that is missing.
            property bool wanted: root.view.unwatchedMissingCount > 0
                                  && root.view.unwatchedMissingCount < root.view.missingCount
            text: "Los no vistos (" + root.view.unwatchedMissingCount + ")"
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
    }
}
