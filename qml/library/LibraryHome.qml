import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library's home, by task: what is ready to watch on disk, what to download and what airs
// next. Each part hides when it has nothing; the whole library is the list beside it, and its
// grid a button away.
Item {
    id: root

    required property AppController app

    readonly property LibraryModel library: app.library
    readonly property bool idle: library.readyCount === 0 && library.downloadCount === 0
                                 && library.upcomingCount === 0

    signal activated(string slug)
    signal browseCatalog()

    // A poster's own button: plays the next episode or queues what the anime is missing.
    function act(slug, action) {
        if (action === "play") {
            app.continueWatching(slug)
            return
        }
        const failure = library.downloadPendingOf(slug)
        if (failure)
            app.message(failure, true)
    }

    // Large batches are confirmed first, as on the web; smaller ones go straight to the queue.
    function downloadAll() {
        const episodes = library.pendingCount
        const series = library.downloadCount
        const start = () => {
            const failure = library.downloadPending()
            if (failure)
                app.message(failure, true)
            else
                app.message(episodes === 1 ? "1 episodio en cola" : episodes + " episodios en cola", false)
        }
        if (episodes < 25) {
            start()
            return
        }
        confirm.ask("¿Descargar " + episodes + " episodios?",
                    "De " + series + (series === 1 ? " anime" : " animes") + ", en "
                    + app.settings.downloadDirectory + ".",
                    "Descargar", "Cancelar", start)
    }

    ConfirmDialog { id: confirm }

    Flickable {
        id: page
        anchors.fill: parent
        visible: !root.idle
        contentHeight: content.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        // Whatever the keyboard reaches comes into view, the agenda's last line too.
        Connections {
            target: root.Window.window
            function onActiveFocusItemChanged() {
                const item = root.Window.activeFocusItem
                let inside = false
                for (let parent = item; parent && !inside; parent = parent.parent)
                    inside = parent === content
                if (!inside || item.focusReason === Qt.MouseFocusReason)
                    return
                const top = item.mapToItem(content, 0, 0).y
                if (top - 12 < page.contentY)
                    page.contentY = Math.max(0, top - 12)
                else if (top + item.height + 12 > page.contentY + page.height)
                    page.contentY = Math.min(top + item.height + 12 - page.height,
                                             Math.max(0, page.contentHeight - page.height))
            }
        }

        Column {
            id: content
            x: Theme.section
            width: page.width - 2 * Theme.section
            topPadding: 28
            bottomPadding: Theme.section
            spacing: Theme.section

            Shelf {
                width: parent.width
                visible: root.library.readyCount > 0
                title: "Para ver"
                count: root.library.readyCount
                model: root.library.readyShelf
                onActivated: slug => root.activated(slug)
                onActed: (slug, episode, action) => root.act(slug, action)
            }

            Shelf {
                width: parent.width
                visible: root.library.downloadCount > 0
                title: "Para descargar"
                count: root.library.downloadCount
                model: root.library.downloadShelf
                onActivated: slug => root.activated(slug)
                onActed: (slug, episode, action) => root.act(slug, action)

                Button {
                    text: root.library.pendingCount === 1 ? "Descargar 1 episodio"
                                                          : "Descargar " + root.library.pendingCount + " episodios"
                    icon.source: Theme.source(Theme.icon.download)
                    onClicked: root.downloadAll()
                }
            }

            UpcomingAgenda {
                width: parent.width
                days: root.library.upcomingDays
                count: root.library.upcomingCount
                onActivated: slug => root.activated(slug)
            }
        }
    }

    // Nothing to do: an empty library, or one seen up to date with nothing announced.
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.section, 420)
        visible: root.idle
        spacing: Theme.pad

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.library.total === 0 ? "Tu biblioteca está vacía" : "Estás al día"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Explorar el catálogo"
            highlighted: true
            onClicked: root.browseCatalog()
        }
    }
}
