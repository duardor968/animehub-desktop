import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One anime: header with its main action, then its episodes. A single scroll.
Item {
    id: page

    required property AnimeView view
    property string backText

    signal back()

    Keys.onEscapePressed: page.back()

    EpisodeMenu {
        id: episodeMenu
        view: page.view
    }

    // A new anime starts at its top; its header grows as images arrive, so keep it there until
    // the user scrolls.
    property bool scrolled: false
    Connections {
        target: page.view
        function onSlugChanged() {
            page.scrolled = false
            episodes.positionViewAtBeginning()
        }
    }

    ListView {
        id: episodes
        anchors.fill: parent
        onMovementStarted: page.scrolled = true
        onOriginYChanged: if (!page.scrolled) positionViewAtBeginning()
        clip: true
        model: page.view.episodes
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        currentIndex: -1
        Accessible.name: "Episodios"

        header: AnimeHeader {
            width: episodes.width
            view: page.view
            backText: page.backText
            onBack: page.back()
        }

        delegate: EpisodeRow {
            width: episodes.width
            view: page.view
            menu: episodeMenu
        }

        footer: Item {
            width: episodes.width
            height: 48

            BusyIndicator {
                anchors.centerIn: parent
                running: page.view.refreshing && episodes.count === 0
                visible: running
            }
        }
    }
}
