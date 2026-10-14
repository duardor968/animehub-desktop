import QtQuick
import QtQuick.Controls
import AnimeHub

// The main action of an anime: watch the next episode, download what is missing, or follow the
// series' downloads with Pausar/Reanudar. Beside it, what else to download. Fluent has no
// SplitButton: as in WinUI, the action and its ▾ are two contiguous buttons.
Row {
    id: root

    required property AnimeView view
    // The compact bar shows the main action alone.
    property bool compact: false

    signal chooseRange()
    signal chooseEpisodes()

    readonly property int action: view.primaryAction
    readonly property bool transferring: action === AnimeView.Downloading
                                         || action === AnimeView.Paused
    readonly property string chevron: "e70d"

    spacing: 8

    Row {
        visible: root.action === AnimeView.Play || root.action === AnimeView.Download
        spacing: 1

        Button {
            id: main
            highlighted: true
            text: root.view.primaryText
            icon.source: Theme.source(root.action === AnimeView.Play ? Theme.icon.play
                                                                     : Theme.icon.download)
            onClicked: root.view.primary()
        }

        Button {
            id: split
            visible: !root.compact && root.action === AnimeView.Download && !root.view.single
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

    // The series' downloads under way: how far they are and, as in Steam, the square button that
    // stops or resumes them.
    Column {
        visible: root.transferring
        anchors.verticalCenter: parent.verticalCenter
        width: root.compact ? 150 : 170
        spacing: 7

        Label {
            width: parent.width
            text: root.view.primaryText
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        ProgressLine {
            width: parent.width
            value: root.view.primaryProgress
            Behavior on value { NumberAnimation { duration: 250 } }
        }
    }

    Button {
        visible: root.transferring
        anchors.verticalCenter: parent.verticalCenter
        highlighted: true
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        display: AbstractButton.IconOnly
        text: root.action === AnimeView.Paused ? "Reanudar" : "Pausar"
        icon.source: Theme.source(root.action === AnimeView.Paused ? Theme.icon.download
                                                                   : Theme.icon.pause)
        Accessible.name: text + " las descargas de " + root.view.title
        ToolTip.visible: hovered
        ToolTip.text: text
        onClicked: root.view.primary()
    }

    // What else to download while the main button does something else; WinUI's DropDownButton
    // puts its chevron after the text, which mirroring the label does.
    Button {
        id: more
        visible: !root.compact && !root.view.single && root.action !== AnimeView.Download
                 && root.view.missingCount > 0
        anchors.verticalCenter: parent.verticalCenter
        text: "Descargar"
        icon.source: Theme.source(root.chevron)
        LayoutMirroring.enabled: true
        onClicked: downloadMenu.show(more)
    }

    OptionsMenu {
        id: downloadMenu

        entries: [missingEntry, unwatchedEntry, rangeEntry, chooseEntry]

        MenuEntry {
            id: missingEntry
            // The main button already does it.
            property bool wanted: root.action !== AnimeView.Download
            enabled: root.view.missingCount > 0
            text: "Todo lo que falta (" + root.view.missingCount + ")"
            onTriggered: root.view.downloadMissing()
        }
        MenuEntry {
            id: unwatchedEntry
            enabled: root.view.unwatchedMissingCount > 0
            text: "Los no vistos (" + root.view.unwatchedMissingCount + ")"
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
