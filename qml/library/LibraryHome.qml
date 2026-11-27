import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The library's home, by task: an atmospheric hero for what to continue watching, "Para ver"
// posters below it, adaptive "Para descargar" tiles or shelf, and "Hoy" pinned in a fixed right
// rail on wide windows (>= 1100 px) or stacked cleanly on narrow ones, always linking to Horario.
Item {
    id: root

    required property AppController app

    readonly property LibraryModel library: app.library
    readonly property bool idle: library.readyCount === 0 && library.downloadCount === 0
                                 && library.upcomingCount === 0
    // Keep the Hoy right rail visible even when upcomingCount === 0 so Horario is always reachable.
    readonly property bool twoZone: (root.Window.window ? root.Window.window.width : width + 300) >= 1100
                                    && width >= 760
    readonly property int railWidth: width >= 980 ? 312 : 280
    readonly property bool compactDownloads: library.downloadCount > 0 && library.downloadCount <= 3

    signal activated(string slug)
    signal browseCatalog()
    signal openSchedule()

    // A poster's or tile's own button: plays the next episode or queues what the anime is missing.
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

    // Whatever the keyboard reaches inside either scroll pane comes cleanly into view.
    function ensureVisible(flick, container, item) {
        if (!item || !flick.visible || !InputModality.keyboard)
            return
        let inside = false
        for (let p = item; p && !inside; p = p.parent)
            inside = p === container
        if (!inside)
            return
        const top = item.mapToItem(container, 0, 0).y
        if (top - 12 < flick.contentY)
            flick.contentY = Math.max(0, top - 12)
        else if (top + item.height + 12 > flick.contentY + flick.height)
            flick.contentY = Math.min(top + item.height + 12 - flick.height,
                                      Math.max(0, flick.contentHeight - flick.height))
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            const item = root.Window.activeFocusItem
            root.ensureVisible(page, content, item)
            if (root.twoZone)
                root.ensureVisible(railFlick, railContent, item)
        }
    }

    ConfirmDialog { id: confirm }

    Flickable {
        id: page
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: root.twoZone ? railSep.left : parent.right
        visible: !root.idle
        contentWidth: width
        contentHeight: content.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        readonly property int sidePad: width >= 680 ? Theme.section : 20

        Column {
            id: content
            x: page.sidePad
            width: Math.min(960, Math.max(0, page.width - 2 * page.sidePad))
            topPadding: 24
            bottomPadding: Theme.section
            spacing: 28

            FeaturedCard {
                id: hero
                width: parent.width
                visible: Boolean(root.library.featured.slug)
                featured: root.library.featured
                onOpen: slug => root.activated(slug)
                onAct: slug => root.app.continueWatching(slug)
                onNext: ready.focusRow()
            }

            Shelf {
                id: ready
                width: parent.width
                visible: root.library.readyShelf.count > 0
                title: "Para ver"
                count: root.library.readyShelf.count
                model: root.library.readyShelf
                lead: null
                onActivated: slug => root.activated(slug)
                onActed: (slug, episode, action) => root.act(slug, action)
                onReachedStart: if (hero.visible) hero.forceFocus(Qt.BacktabFocusReason)
            }

            // Compact horizontal action tiles for 1..3 pending anime (eliminates empty shelf void).
            Column {
                id: compactDownloadSection
                width: parent.width
                visible: root.compactDownloads
                spacing: 12

                RowLayout {
                    width: parent.width
                    spacing: 8

                    Label {
                        text: "Para descargar"
                        color: Theme.ink
                        font.pixelSize: Theme.subtitle
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
                    }

                    Label {
                        text: "(" + root.library.downloadCount + ")"
                        color: Theme.muted
                        font.pixelSize: Theme.subtitle
                    }

                    Button {
                        visible: root.library.pendingCount > 1
                        Layout.leftMargin: 12
                        text: "Descargar " + root.library.pendingCount + " episodios"
                        icon.source: Theme.source(Theme.icon.download)
                        onClicked: root.downloadAll()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        Layout.preferredHeight: 1
                        color: Theme.stroke
                    }
                }

                Flow {
                    id: dlFlow
                    width: parent.width
                    spacing: 12

                    readonly property int maxCols: Math.max(1, Math.floor((width + spacing) / (340 + spacing)))
                    readonly property int cols: Math.max(1, Math.min(root.library.downloadCount, maxCols))
                    readonly property real tileWidth: cols === 1
                                                      ? Math.min(width, 460)
                                                      : (width - (cols - 1) * spacing) / cols

                    Repeater {
                        model: root.compactDownloads ? root.library.downloadShelf : null

                        delegate: Item {
                            id: tile

                            required property var model

                            width: dlFlow.tileWidth
                            height: 84

                            // Card surface opens the anime; sibling Download button acts directly.
                            AbstractButton {
                                id: tileCard
                                anchors.fill: parent
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                leftPadding: 10
                                topPadding: 10
                                bottomPadding: 10
                                rightPadding: tileAction.width + 22
                                text: tile.model.title
                                Accessible.name: tile.model.title + ", " + tile.model.caption
                                onClicked: root.activated(tile.model.slug)
                                Keys.onReturnPressed: clicked()
                                Keys.onEnterPressed: clicked()
                                ToolTip.visible: hovered && tileTitle.truncated
                                ToolTip.delay: 500
                                ToolTip.text: tile.model.title

                                background: Rectangle {
                                    radius: Theme.radius
                                    color: tileCard.hovered || tileAction.hovered ? Theme.hover : Theme.raised
                                    border.width: 1
                                    border.color: tileCard.hovered || tileAction.hovered ? "#383f4a" : Theme.stroke
                                    Behavior on color { ColorAnimation { duration: 120 } }
                                }

                                contentItem: RowLayout {
                                    spacing: 12

                                    Poster {
                                        Layout.preferredWidth: 46
                                        Layout.preferredHeight: 64
                                        Layout.alignment: Qt.AlignVCenter
                                        source: tile.model.posterUrl
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.alignment: Qt.AlignVCenter
                                        spacing: 3

                                        Label {
                                            id: tileTitle
                                            Layout.fillWidth: true
                                            text: tile.model.title
                                            color: Theme.ink
                                            font.pixelSize: Theme.body
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: tile.model.caption
                                            color: Theme.muted
                                            font.pixelSize: Theme.caption
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }
                                    }
                                }

                                FocusFrame {
                                    target: tileCard
                                    radiusInside: Theme.radius
                                    visible: tileCard.activeFocus && InputModality.keyboard
                                }
                            }

                            Button {
                                id: tileAction
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Descargar"
                                icon.source: Theme.source(Theme.icon.download)
                                onClicked: root.act(tile.model.slug, "download")
                                Keys.onReturnPressed: clicked()
                                Keys.onEnterPressed: clicked()
                                Accessible.name: "Descargar " + tile.model.title
                            }
                        }
                    }
                }
            }

            // Standard poster shelf when 4 or more anime have pending downloads.
            Shelf {
                id: downloadShelf
                width: parent.width
                visible: root.library.downloadCount > 3
                title: "Para descargar"
                count: root.library.downloadCount
                model: root.library.downloadCount > 3 ? root.library.downloadShelf : null
                onActivated: slug => root.activated(slug)
                onActed: (slug, episode, action) => root.act(slug, action)
                onReachedStart: if (!ready.visible && hero.visible) hero.forceFocus(Qt.BacktabFocusReason)

                Button {
                    visible: root.library.pendingCount > 1
                    text: "Descargar " + root.library.pendingCount + " episodios"
                    icon.source: Theme.source(Theme.icon.download)
                    onClicked: root.downloadAll()
                }
            }

            // Narrow-window stacked Hoy section (hidden when the fixed right rail is active).
            UpcomingAgenda {
                id: stackedAgenda
                width: parent.width
                visible: !root.twoZone
                rail: false
                heading: root.library.todayHeading
                items: !root.twoZone ? root.library.upcoming : []
                stale: root.library.scheduleStale
                onActivated: slug => root.activated(slug)
                onOpenSchedule: root.openSchedule()
            }
        }
    }

    Rectangle {
        id: railSep
        visible: !root.idle && root.twoZone
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: railPane.left
        width: 1
        color: Theme.stroke
    }

    // Fixed right "Hoy" rail on wide windows (>= 1100 px), always offering "Ver horario ›".
    Rectangle {
        id: railPane
        visible: !root.idle && root.twoZone
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: root.railWidth
        color: Theme.pane

        Flickable {
            id: railFlick
            anchors.fill: parent
            contentWidth: width
            contentHeight: railContent.height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}

            Item {
                id: railContent
                readonly property int railPad: root.railWidth >= 300 ? 20 : 16
                width: railFlick.width
                height: railAgenda.implicitHeight + 2 * railPad

                UpcomingAgenda {
                    id: railAgenda
                    x: railContent.railPad
                    y: railContent.railPad
                    width: parent.width - 2 * railContent.railPad
                    rail: true
                    heading: root.library.todayHeading
                    items: root.twoZone ? root.library.upcoming : []
                    stale: root.library.scheduleStale
                    onActivated: slug => root.activated(slug)
                    onOpenSchedule: root.openSchedule()
                }
            }
        }
    }

    // Nothing to do: an empty library, or one seen up to date with nothing announced today.
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

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            AccentButton {
                text: "Explorar el catálogo"
                onClicked: root.browseCatalog()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
            }

            Button {
                visible: root.library.total > 0
                text: "Ver horario"
                icon.source: Theme.source(Theme.icon.schedule)
                onClicked: root.openSchedule()
            }
        }
    }
}
