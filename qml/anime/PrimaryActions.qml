import QtQuick
import QtQuick.Controls
import AnimeHub

// The main button, a state of the episode to watch next ("Ver ep. 9", "Descargar ep. 9",
// "Descargando 45 %"…), and for a series what else to download. Fluent has no SplitButton: as in
// WinUI, the action and its ▾ are two contiguous buttons.
Row {
    id: root

    required property AnimeView view

    signal chooseRange()
    signal chooseEpisodes()

    readonly property int action: view.primaryAction
    readonly property string chevron: "e70d"
    // The one episode missing is the next one: the main button already gets it, nothing is left
    // to choose.
    readonly property bool onlyNextMissing: view.missingCount === 1
                                            && (action === AnimeView.Download || action === AnimeView.Paused
                                                || action === AnimeView.Failed)
    // What else there is to download, for a series.
    readonly property bool offersMore: !view.single && view.missingCount > 0 && !onlyNextMissing

    spacing: 8

    Row {
        visible: root.action !== AnimeView.None
        spacing: 1

        Button {
            id: main

            readonly property bool transferring: root.action === AnimeView.Downloading
                                                 || root.action === AnimeView.Paused

            highlighted: true
            // A series up to date waits for its next episode: the button says when.
            enabled: root.action !== AnimeView.Upcoming
            // One width for the usual states: the figures beside it stay put as it changes.
            implicitWidth: Math.max(176, implicitContentWidth + leftPadding + rightPadding)
            text: root.view.primaryText
            // What a press does; the text says where the episode is.
            icon.source: Theme.source(({
                [AnimeView.Play]: Theme.icon.play,
                [AnimeView.Rewatch]: Theme.icon.play,
                [AnimeView.Download]: Theme.icon.download,
                [AnimeView.Queued]: Theme.icon.pause,
                [AnimeView.Downloading]: Theme.icon.pause,
                [AnimeView.Paused]: Theme.icon.download,
                [AnimeView.Failed]: Theme.icon.retry,
                [AnimeView.Upcoming]: Theme.icon.clock
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
                color: Qt.rgba(1, 1, 1, 0.3)

                Rectangle {
                    width: parent.width * root.view.primaryProgress
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accentInk
                    Behavior on width { NumberAnimation { duration: 250 } }
                }
            }
        }

        Button {
            id: split
            visible: root.action === AnimeView.Download && root.offersMore
            highlighted: true
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
    }

    // What else to download while the main button does something else; WinUI's DropDownButton
    // puts its chevron after the text, which mirroring the label does.
    Button {
        id: more
        visible: root.action !== AnimeView.Download && root.offersMore
        anchors.verticalCenter: parent.verticalCenter
        text: "Descargar"
        icon.source: Theme.source(root.chevron)
        LayoutMirroring.enabled: true
        onClicked: downloadMenu.show(more)
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
            text: "Elegir episodios"
            onTriggered: root.chooseEpisodes()
        }
    }
}
