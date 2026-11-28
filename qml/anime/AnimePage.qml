import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One anime, as Steam shows a game: the compact hero header, the sticky single-row action bar,
// the episodes in pages of fifty beside a sticky right detail column (Sinopsis, Contenido sensible,
// Detalles with AnimeAV1 attribution, trailer & source links) that stays cleanly below ActionBar
// while scrolling and clears the Relacionados shelf at the foot.
Item {
    id: page

    required property AnimeView view
    // Where the page was opened from, for the breadcrumb over the title: [{text, go}].
    property var steps: []

    // Esc goes back as the window's ← does; ← and → go anywhere it has been.
    signal back()
    // Clicking a genre chip routes to Catálogo filtered by that genre slug.
    signal genreSelected(string slug)

    // "Elegir episodios": every row shows its selection mark hint until the selection ends.
    property bool choosing: false
    readonly property bool selecting: choosing || view.episodes.selectedCount > 0
    // One column from the left margin capped at 1280 px, so the header, action bar, episodes,
    // detail column and related shelf share one crisp right edge even on ultrawide displays.
    readonly property real contentWidth: Math.max(0, Math.min(list.width - 2 * Theme.section, 1280))
    // From 960 px, two columns as on a store page: the episodes, and beside them Sinopsis + Detalles.
    readonly property bool twoColumns: !view.single && list.width >= 960
    readonly property real sideWidth: list.width >= 1100 ? 320 : 296
    readonly property real sideGap: list.width >= 1100 ? 40 : 32
    readonly property real mainWidth: twoColumns ? contentWidth - sideWidth - sideGap
                                                 : contentWidth
    readonly property bool hasLinks: view.trailerUrl.toString() !== "" || view.sourceUrl !== ""
    readonly property bool hasSideInfo: view.synopsis !== "" || view.mature
                                        || view.hasDetails || hasLinks
    // Natural bottom of the side column in ListView content coordinates at scroll 0, so the
    // Relacionados shelf always starts cleanly underneath even on anime with few episodes.
    readonly property real sideNaturalBottom: list.headerItem && twoColumns && hasSideInfo
                                              ? list.headerItem.y + list.headerItem.barY
                                                + actionBar.unpinnedHeight + 20
                                                + sideContent.implicitHeight
                                              : 0

    // System font probe for selectable TextEdits so Windows and Linux match Fluent's UI font.
    Label {
        id: fontProbe
        visible: false
    }

    // Selectable synopsis: collapsed to `lines` when very long, genuinely mouse-selectable in all states.
    component Synopsis: Column {
        id: about

        property int lines: 3
        property bool expanded: false
        property Flickable scroller: null
        property real topClearance: 12
        // Each anime opens collapsed.
        readonly property var paragraphs: page.view.synopsis.split(/\n\s*\n/)
        readonly property string shownText: about.expanded ? page.view.synopsis : about.paragraphs[0]
        readonly property real singleLineH: synopsisEdit.lineCount > 0
                                            ? synopsisEdit.contentHeight / synopsisEdit.lineCount
                                            : 18
        readonly property bool lineOverflow: !about.expanded && synopsisEdit.lineCount > about.lines
        readonly property bool canExpand: about.expanded || about.lineOverflow
                                          || about.paragraphs.length > 1

        onParagraphsChanged: expanded = false

        spacing: 4
        visible: page.view.synopsis !== ""

        Item {
            width: Math.min(about.width, 900)
            height: width > 0
                    ? (about.lineOverflow
                       ? Math.ceil(about.singleLineH * about.lines)
                       : Math.ceil(synopsisEdit.contentHeight))
                    : 0
            clip: about.lineOverflow

            TextEdit {
                id: synopsisEdit
                width: parent.width
                readOnly: true
                selectByMouse: true
                activeFocusOnTab: false
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentInk
                text: about.shownText
                color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.88)
                font.family: fontProbe.font.family
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                HoverHandler { cursorShape: Qt.IBeamCursor }
            }

            // Soft bottom fade when clamped so users immediately see more text awaits expansion.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 22
                visible: about.lineOverflow
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.canvas }
                }
            }
        }

        Button {
            id: expandButton
            visible: about.canExpand
            flat: true
            text: about.expanded ? "Mostrar menos" : "Mostrar más"
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            onActiveFocusChanged: if (activeFocus && InputModality.keyboard)
                                      page.ensureVisibleIn(about.scroller, expandButton,
                                                           about.topClearance, 16)
            onClicked: about.expanded = !about.expanded
            // Its text in the synopsis column; the hover background may overhang, as in WinUI.
            x: -leftPadding
        }
    }

    // Rich detail block: selectable Sinopsis, optional Contenido sensible badge, borderless
    // Detalles key-value rows (including Puntuación (AnimeAV1)), and direct trailer/AnimeAV1 buttons.
    component DetailsColumn: ColumnLayout {
        id: detailsRoot

        property int synopsisLines: 10
        property Flickable scroller: null
        property real topClearance: 12
        readonly property bool hasTop: page.view.synopsis !== "" || page.view.mature
        readonly property bool hasBottom: page.view.hasDetails || page.hasLinks
        readonly property var detailCells: {
            const cells = []
            const rows = page.view.details || []
            for (let i = 0; i < rows.length; ++i) {
                cells.push({ isValue: false, text: rows[i].label ?? "", icon: "" })
                cells.push({ isValue: true, text: rows[i].value ?? "", icon: rows[i].icon ?? "" })
            }
            return cells
        }

        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            visible: page.view.synopsis !== ""
            spacing: 6

            Label {
                text: "Sinopsis"
                color: Theme.ink
                font.pixelSize: Theme.body
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }

            Synopsis {
                Layout.fillWidth: true
                lines: detailsRoot.synopsisLines
                scroller: detailsRoot.scroller
                topClearance: detailsRoot.topClearance
            }
        }

        // Soft warning badge shown ONLY when the API marks the anime as mature.
        Rectangle {
            visible: page.view.mature
            implicitWidth: matureRow.implicitWidth + 16
            implicitHeight: 24
            radius: Theme.radiusSm
            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
            border.width: 1
            border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.30)
            Accessible.role: Accessible.StaticText
            Accessible.name: "Contenido sensible"

            RowLayout {
                id: matureRow
                anchors.centerIn: parent
                spacing: 6

                Glyph {
                    glyph: Theme.icon.warning
                    size: 12
                    color: Theme.warning
                }

                Label {
                    text: "Contenido sensible"
                    color: Theme.warning
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                    Accessible.ignored: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: detailsRoot.hasTop && detailsRoot.hasBottom
            Layout.preferredHeight: 1
            color: Theme.stroke
            opacity: 0.7
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: detailsRoot.hasBottom
            spacing: 10

            Label {
                visible: page.view.hasDetails
                text: "Detalles"
                color: Theme.ink
                font.pixelSize: Theme.body
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }

            // Borderless key-value rows aligned on two clean columns (zero box-inside-box).
            GridLayout {
                Layout.fillWidth: true
                visible: page.view.hasDetails
                columns: 2
                columnSpacing: 14
                rowSpacing: 7

                Repeater {
                    model: detailsRoot.detailCells

                    delegate: RowLayout {
                        id: detailCell
                        required property var modelData
                        Layout.fillWidth: modelData.isValue
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: 5

                        Glyph {
                            visible: detailCell.modelData.isValue
                                     && detailCell.modelData.icon === "starFilled"
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            glyph: Theme.icon.starFilled
                            size: Theme.small
                            color: Theme.warning
                        }

                        Label {
                            Layout.fillWidth: detailCell.modelData.isValue
                            Layout.minimumWidth: 0
                            Layout.alignment: Qt.AlignTop
                            text: detailCell.modelData.text
                            color: detailCell.modelData.isValue ? Theme.ink : Theme.muted
                            font.pixelSize: Theme.small
                            font.weight: detailCell.modelData.isValue ? Font.DemiBold : Font.Normal
                            wrapMode: detailCell.modelData.isValue ? Text.Wrap : Text.NoWrap
                        }
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.topMargin: page.view.hasDetails ? 6 : 0
                visible: page.hasLinks
                spacing: 8

                AccentButton {
                    id: trailerButton
                    visible: page.view.trailerUrl.toString() !== ""
                    emphasized: false
                    glyphCode: Theme.icon.video
                    iconSize: 14
                    fontPixelSize: Theme.small
                    implicitHeight: 30
                    leftPadding: 12
                    rightPadding: 12
                    text: "Ver tráiler"
                    onActiveFocusChanged: if (activeFocus && InputModality.keyboard)
                                              page.ensureVisibleIn(detailsRoot.scroller, trailerButton,
                                                                   detailsRoot.topClearance, 16)
                    onClicked: page.view.openTrailer()
                }

                AccentButton {
                    id: sourceButton
                    visible: page.view.sourceUrl !== ""
                    emphasized: false
                    glyphCode: Theme.icon.openExternal
                    iconSize: 14
                    fontPixelSize: Theme.small
                    implicitHeight: 30
                    leftPadding: 12
                    rightPadding: 12
                    text: "Ver en AnimeAV1"
                    onActiveFocusChanged: if (activeFocus && InputModality.keyboard)
                                              page.ensureVisibleIn(detailsRoot.scroller, sourceButton,
                                                                   detailsRoot.topClearance, 16)
                    onClicked: page.view.openSource()
                }
            }
        }
    }

    function ensureVisibleIn(flick, item, topClearance, bottomClearance) {
        if (!flick || !item || !flick.contentItem)
            return
        const pos = item.mapToItem(flick.contentItem, 0, 0)
        const top = pos.y - topClearance
        const bottom = pos.y + item.height + bottomClearance
        const minY = flick.originY
        const maxY = Math.max(minY, flick.originY + flick.contentHeight + flick.bottomMargin - flick.height)
        if (top < flick.contentY) {
            if (flick === list)
                page.scrolled = true
            flick.contentY = Math.max(minY, Math.min(top, maxY))
        } else if (bottom > flick.contentY + flick.height) {
            if (flick === list)
                page.scrolled = true
            flick.contentY = Math.max(minY, Math.min(bottom - flick.height, maxY))
        }
    }

    function focusTitle(reason) {
        if (list.headerItem)
            list.headerItem.focusTitle(reason !== undefined ? reason : InputModality.reason)
    }

    function clearSelection() {
        view.episodes.clearSelection()
        choosing = false
    }

    // The list stays where it is when its head is already on view.
    function chooseEpisodes() {
        choosing = true
        const header = list.headerItem
        if (header && header.y + header.episodesY - list.contentY < actionBar.singleRowHeight)
            scrollToEpisodes(true)
    }

    // The head of the episode list at the top of the view, under the pinned single-row bar.
    function scrollToEpisodes(animated) {
        const header = list.headerItem
        if (!header)
            return
        // The room kept under the last row counts: with it, no row stays under the selection bar.
        const bottom = list.originY + list.contentHeight + list.bottomMargin - list.height
        const top = header.y + header.episodesY - actionBar.singleRowHeight - 12
        const target = Math.max(list.originY, Math.min(top, bottom))
        page.scrolled = true
        if (animated) {
            scroll.to = target
            scroll.restart()
        } else {
            list.contentY = target
        }
    }

    Keys.onEscapePressed: page.selecting ? page.clearSelection() : page.back()

    EpisodeMenu {
        id: episodeMenu
        view: page.view
    }

    ConfirmDialog {
        id: confirm
    }

    RangeDialog {
        id: rangeDialog
        view: page.view
    }

    // A new anime starts at its top; its header settles as images arrive, so keep it there
    // until the user scrolls.
    property bool scrolled: false
    Connections {
        target: page.view
        function onSlugChanged() {
            page.scrolled = false
            page.choosing = false
            sideFlick.contentY = 0
            list.positionViewAtBeginning()
        }
        function onDownloadConfirmationNeeded(numbers, question, detail, selection) {
            confirm.ask(question, detail, "Descargar", "Cancelar", () => {
                page.view.downloadEpisodes(numbers)
                if (selection)
                    page.clearSelection()
            })
        }
        function onCancelConfirmationNeeded(number, question, detail) {
            if (number < 0 && !page.view.single) {
                const count = Math.max(0, page.view.episodes.total
                                       - page.view.onDiskCount - page.view.missingCount)
                const scope = count === 1 ? "Cancelar 1 episodio"
                              : count > 1 ? "Cancelar " + count + " episodios"
                              : "Cancelar descargas"
                const finalQuestion = count > 0
                                      ? "¿" + scope + " de " + page.view.title + "?"
                                      : question
                const kept = page.view.onDiskCount === 1
                             ? " El episodio ya completado se queda en disco."
                             : page.view.onDiskCount > 1
                               ? " Los " + page.view.onDiskCount + " episodios ya completados se quedan en disco."
                               : " Los episodios ya completados se quedan en disco."
                confirm.ask(finalQuestion, detail + kept, scope, "Seguir descargando", () => {
                    page.view.cancelSeries(true)
                })
                return
            }
            confirm.ask(question, detail, "Cancelar la descarga", "Seguir descargando", () => {
                if (number < 0)
                    page.view.cancelSeries(true)
                else
                    page.view.cancelDownload(number, true)
            })
        }
    }

    // Choosing ends once the chosen episodes have been acted on.
    property int chosen: 0
    Connections {
        target: page.view.episodes
        function onSelectionChanged() {
            const count = page.view.episodes.selectedCount
            if (count === 0 && page.chosen > 0)
                page.choosing = false
            page.chosen = count
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        onMovementStarted: page.scrolled = true
        onOriginYChanged: if (!page.scrolled) positionViewAtBeginning()
        clip: true
        // A film has no list: everything about it is above.
        model: page.view.single ? null : page.view.episodes
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        currentIndex: -1
        // Room for the selection bar under the last row.
        bottomMargin: page.selecting ? selectionBar.height + 24 : 0
        Accessible.name: "Episodios"

        NumberAnimation {
            id: scroll
            target: list
            property: "contentY"
            duration: 300
            easing.type: Easing.OutCubic
        }

        header: Column {
            id: overview

            readonly property real barY: barSlot.y
            readonly property real episodesY: episodesBar.y

            function focusTitle(reason) { animeHeader.focusTitle(reason) }

            width: list.width
            onHeightChanged: if (!page.scrolled) Qt.callLater(() => { if (!page.scrolled) list.positionViewAtBeginning() })

            readonly property Item titleItem: animeHeader.titleItem

            AnimeHeader {
                id: animeHeader
                width: parent.width
                steps: page.steps
                bar: actionBar
                contentWidth: page.contentWidth
                title: page.view.title
                subtitle: page.view.alternativeTitle
                score: page.view.score
                scoreText: page.view.scoreText
                scoreSource: page.view.scoreSource
                facts: page.view.facts
                statusText: page.view.statusText
                statusTone: page.view.statusTone
                metaLine: page.view.metaLine
                genres: page.view.genres
                genreChips: page.view.genreChips
                posterUrl: page.view.posterUrl
                backdropUrl: page.view.backdropUrl
                onGenreSelected: slug => page.genreSelected(slug)
            }

            // Reserves the unpinned bar's band so pinning never shifts list.contentY.
            Item {
                id: barSlot
                width: parent.width
                height: actionBar.unpinnedHeight
            }

            ColumnLayout {
                x: Theme.section
                width: page.mainWidth
                spacing: 12

                // Room above the first line, whichever it is.
                Item {
                    Layout.preferredHeight: 10
                }

                RowLayout {
                    visible: page.view.problem !== ""
                    spacing: 10

                    Glyph {
                        glyph: Theme.icon.warning
                        color: Theme.warning
                    }

                    Label {
                        text: page.view.problem
                        color: Theme.ink
                    }

                    Button {
                        id: retryButton
                        text: "Reintentar"
                        Accessible.onPressAction: click()
                        Keys.onReturnPressed: event => { event.accepted = true; click() }
                        Keys.onEnterPressed: event => { event.accepted = true; click() }
                        onActiveFocusChanged: if (activeFocus && InputModality.keyboard)
                                                  page.ensureVisibleIn(list, retryButton,
                                                                       actionBar.singleRowHeight + 12, 16)
                        onClicked: page.view.reload()
                    }
                }

                // Single-episode films have no episode list, so Sinopsis + Detalles sit directly
                // below ActionBar; narrow multi-episode shows place DetailsColumn in footerColumn
                // so Episodios stays immediately visible above the fold at 820×600.
                Loader {
                    Layout.fillWidth: true
                    active: !page.twoColumns && page.view.single && page.hasSideInfo
                    visible: active
                    sourceComponent: Component {
                        DetailsColumn {
                            synopsisLines: 10
                            scroller: list
                            topClearance: actionBar.singleRowHeight + 12
                        }
                    }
                }

                // Without episodes yet, where their list goes, what is true of them: on their way,
                // or none to list, in the text's column.
                Label {
                    visible: page.view.loaded && page.view.problem === ""
                             && page.view.episodes.total === 0
                    text: page.view.refreshing ? "Cargando los episodios…"
                                               : "Aún no hay episodios disponibles"
                    color: Theme.muted
                }

                Item {
                    Layout.preferredHeight: 4
                }
            }

            EpisodesBar {
                id: episodesBar
                x: Theme.section
                width: page.mainWidth
                visible: !page.view.single && page.view.episodes.total > 0
                view: page.view
                selecting: page.selecting
                onMoved: page.scrollToEpisodes(false)
            }
        }

        delegate: EpisodeRow {
            // Bound the delegate strictly to the episodes column (+12 px hover overhang) so its
            // hit box never overlaps the right detail column.
            width: Theme.section + page.mainWidth + 12
            leftPadding: Theme.section
            rightPadding: 12
            leftInset: Theme.section - 12
            rightInset: 0
            view: page.view
            menu: episodeMenu
            selecting: page.selecting
            onKeyboardFocused: target => page.ensureVisibleIn(list, target,
                                                              actionBar.singleRowHeight + 12, 16)
        }

        footer: Column {
            id: footerColumn

            readonly property AnimeView view: page.view
            readonly property bool hasFullWidthFooter: related.visible
            readonly property real fullWidthTop: y + related.y

            width: list.width
            // Below the side column when the episodes end before it does.
            topPadding: page.twoColumns && page.hasSideInfo
                        ? Math.max(12, page.sideNaturalBottom - y + 24)
                        : 12
            bottomPadding: Theme.section
            spacing: Theme.section

            Item {
                width: parent.width
                height: bottomPager.implicitHeight
                visible: bottomPager.visible

                Pager {
                    id: bottomPager
                    x: Theme.section + page.mainWidth - width
                    visible: !page.view.single && page.view.episodes.pageCount > 1
                    episodes: page.view.episodes
                    onMoved: page.scrollToEpisodes(false)
                }
            }

            // Narrow multi-episode fallback: keep Sinopsis, Detalles and links cleanly below
            // episodes (above Relacionados) so Episodios appears immediately above the fold.
            Loader {
                x: Theme.section
                width: page.mainWidth
                active: !page.twoColumns && !page.view.single && page.hasSideInfo
                visible: active
                sourceComponent: Component {
                    DetailsColumn {
                        synopsisLines: 5
                        scroller: list
                        topClearance: actionBar.singleRowHeight + 12
                    }
                }
            }

            Shelf {
                id: related
                x: Theme.section
                width: page.contentWidth
                title: "Relacionados"
                // Not `page.view`: inside a Shelf, `page` is its own paging function.
                model: footerColumn.view.related
                onActivated: slug => {
                    // Main.qml opens it in the library when it is there, else in the catalog.
                    const window = ApplicationWindow.window
                    if (window && typeof window.openAnime === "function")
                        window.openAnime(slug)
                }
            }
        }
    }

    // Sticky right detail column: sits cleanly 20 px below ActionBar while scrolling episodes
    // (ZERO overlap or horizontal slicing behind ActionBar), scrolls internally if taller than the
    // viewport, and slides upward out of the way when the full-width Relacionados shelf arrives.
    Item {
        id: sideViewport
        z: 5
        visible: page.twoColumns && page.hasSideInfo && list.headerItem !== null
        x: Theme.section + page.mainWidth + page.sideGap
        y: actionBar.y + actionBar.height
        width: page.sideWidth
        height: Math.max(0, page.height - y - (page.selecting ? selectionBar.height + 24 : 0))
        clip: true

        readonly property real shelfTopLocal: list.footerItem && list.footerItem.hasFullWidthFooter
                                              ? (list.footerItem.fullWidthTop - list.contentY) - y
                                              : height + 10000
        readonly property real desiredHeight: Math.min(Math.ceil(sideContent.implicitHeight),
                                                       Math.max(0, height - 36))
        readonly property real maxYBeforeShelf: shelfTopLocal - desiredHeight - 24

        Flickable {
            id: sideFlick
            x: 0
            y: Math.min(20, sideViewport.maxYBeforeShelf)
            width: parent.width
            height: sideViewport.desiredHeight
            contentWidth: width
            contentHeight: sideContent.implicitHeight
            clip: true
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: sideFlick.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            DetailsColumn {
                id: sideContent
                width: sideFlick.width
                synopsisLines: 10
                scroller: sideFlick
                topClearance: 12
            }
        }
    }

    ActionBar {
        id: actionBar
        z: 10

        // The download figure's only action: "Reintentar ahora".
        onFigureAction: page.view.retryWaiting()

        // Where the header leaves it, until it reaches the top.
        readonly property real slotY: {
            const header = list.headerItem
            return header ? header.y + header.barY - list.contentY : 0
        }

        anchors.left: parent.left
        anchors.right: parent.right
        y: Math.max(0, slotY)
        pinned: slotY < 0
        visible: list.headerItem !== null
        contentWidth: page.contentWidth
        title: page.view.title
        titleItem: list.headerItem ? list.headerItem.titleItem : null
        posterUrl: page.view.posterUrl
        figures: page.view.figures

        PrimaryActions {
            id: primaryActions
            view: page.view
            selecting: page.selecting
            compact: actionBar.compact
            onChooseRange: rangeDialog.open()
            onChooseEpisodes: page.chooseEpisodes()
        }

        // Watching or downloading also adds it; this is for keeping it without either.
        Button {
            visible: page.view.loaded && !page.view.inLibrary
            text: "Añadir a la biblioteca"
            icon.source: Theme.source(Theme.icon.add)
            Accessible.onPressAction: click()
            Keys.onReturnPressed: event => { event.accepted = true; click() }
            Keys.onEnterPressed: event => { event.accepted = true; click() }
            onClicked: page.view.setInLibrary(true)
        }

        trailing: [
            ToolButton {
                visible: page.view.loaded
                icon.source: Theme.source(page.view.favorite ? Theme.icon.starFilled : Theme.icon.star)
                icon.color: page.view.favorite ? Theme.warning : Theme.ink
                display: AbstractButton.IconOnly
                text: page.view.favorite ? "Quitar de favoritos" : "Añadir a favoritos"
                Accessible.checkable: true
                Accessible.checked: page.view.favorite
                Accessible.onPressAction: click()
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Keys.onReturnPressed: event => { event.accepted = true; click() }
                Keys.onEnterPressed: event => { event.accepted = true; click() }
                onClicked: page.view.setFavorite(!page.view.favorite)
            },
            ToolButton {
                visible: page.view.airing && page.view.inLibrary
                icon.source: Theme.source(page.view.notify ? Theme.icon.bell : Theme.icon.bellOff)
                display: AbstractButton.IconOnly
                text: page.view.notify ? "Dejar de avisar de episodios nuevos"
                                       : "Avisar de episodios nuevos"
                Accessible.checkable: true
                Accessible.checked: page.view.notify
                Accessible.onPressAction: click()
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Keys.onReturnPressed: event => { event.accepted = true; click() }
                Keys.onEnterPressed: event => { event.accepted = true; click() }
                onClicked: page.view.setNotify(!page.view.notify)
            },
            ToolButton {
                id: moreButton
                visible: page.view.loaded
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                text: "Más opciones"
                Accessible.onPressAction: click()
                ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: text
                Keys.onReturnPressed: event => { event.accepted = true; click() }
                Keys.onEnterPressed: event => { event.accepted = true; click() }
                onClicked: {
                    moreMenu.folderExists = page.view.hasFolder()
                    moreMenu.show(moreButton, true)
                }
            }
        ]
    }

    // What the bar's buttons do not do, by kind: watching, downloading, the rest, and what removes
    // things, last and apart.
    ActionMenu {
        id: moreMenu

        // Checked when the menu opens: the folder appears with the first download.
        property bool folderExists: false

        entries: [watchedEntry, unwatchedEntry, positionEntry,
                  downloadsSeparator, autoEntry, pauseEntry, resumeEntry, retryEntry,
                  otherSeparator, trailerEntry, sourceEntry, folderEntry,
                  removalSeparator, cancelEntry, trashWatchedEntry, trashFilmEntry, removeEntry]

        MenuEntry {
            id: watchedEntry
            property bool wanted: page.view.watchedProgress < 1
            text: page.view.single ? "Marcar como visto" : "Marcar todo como visto"
            glyph: Theme.icon.check
            onTriggered: page.view.markAllWatched()
        }
        MenuEntry {
            id: unwatchedEntry
            property bool wanted: page.view.started
            text: page.view.single ? "Marcar como no visto" : "Marcar todo como no visto"
            glyph: Theme.icon.undo
            onTriggered: page.view.markAllUnwatched()
        }
        MenuEntry {
            id: positionEntry
            property bool wanted: page.view.single
            text: "Indicar dónde te quedaste…"
            glyph: Theme.icon.clock
            onTriggered: episodeMenu.askPosition(page.view.firstNumber, page.view.title)
        }
        MenuSeparator {
            id: downloadsSeparator
            property bool separator: true
        }
        // Its check says whether it is on. Only for anime in the library, the ones watched for
        // new episodes.
        MenuEntry {
            id: autoEntry
            property bool wanted: page.view.airing && page.view.inLibrary
            text: "Descargar los episodios nuevos"
            checkable: true
            checked: page.view.autoDownload
            onTriggered: {
                if (checked) {
                    checked = false // the user confirms first
                    autoDownloadDialog.open()
                } else {
                    page.view.setAutoDownload(false)
                }
            }
        }
        // The series' downloads, when the main button does not already pause or resume them.
        MenuEntry {
            id: pauseEntry
            property bool wanted: !page.view.single && page.view.downloadRunning
                                  && page.view.primaryAction !== AnimeView.PauseAll
            text: "Pausar descargas"
            glyph: Theme.icon.pause
            onTriggered: page.view.pauseSeries()
        }
        MenuEntry {
            id: resumeEntry
            property bool wanted: !page.view.single && page.view.downloadPaused
                                  && page.view.primaryAction !== AnimeView.ResumeAll
            text: "Reanudar descargas"
            // Play is for watching: resuming a download keeps the download glyph.
            glyph: Theme.icon.download
            onTriggered: page.view.resumeSeries()
        }
        MenuEntry {
            id: retryEntry
            // Beyond the next episode's, which the main button retries.
            property bool wanted: !page.view.single && page.view.failedCount
                                  > (page.view.primaryAction === AnimeView.Failed ? 1 : 0)
            text: "Reintentar fallidos"
            glyph: Theme.icon.retry
            onTriggered: page.view.retryFailed()
        }
        MenuSeparator {
            id: otherSeparator
            property bool separator: true
        }
        MenuEntry {
            id: trailerEntry
            property bool wanted: page.view.trailerUrl.toString() !== ""
            text: "Ver tráiler"
            glyph: Theme.icon.video
            onTriggered: page.view.openTrailer()
        }
        MenuEntry {
            id: sourceEntry
            property bool wanted: page.view.sourceUrl !== ""
            text: "Ver en AnimeAV1"
            glyph: Theme.icon.openExternal
            onTriggered: page.view.openSource()
        }
        MenuEntry {
            id: folderEntry
            property bool wanted: moreMenu.folderExists
            text: "Abrir la carpeta"
            glyph: Theme.icon.folder
            onTriggered: page.view.openFolder()
        }
        MenuSeparator {
            id: removalSeparator
            property bool separator: true
        }
        MenuEntry {
            id: cancelEntry
            property bool wanted: page.view.hasDownloads
            text: page.view.single ? "Cancelar la descarga" : "Cancelar descargas"
            glyph: Theme.icon.cancel
            onTriggered: page.view.cancelSeries(false)
        }
        MenuEntry {
            id: trashWatchedEntry
            property bool wanted: page.view.watchedOnDisk && !page.view.single
            text: "Mover los vistos a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: page.view.trashWatched()
        }
        MenuEntry {
            id: trashFilmEntry
            property bool wanted: page.view.single && page.view.onDiskCount > 0
            text: "Mover a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: page.view.trashFile(page.view.firstNumber)
        }
        MenuEntry {
            id: removeEntry
            property bool wanted: page.view.inLibrary
            text: "Quitar de la biblioteca…"
            glyph: Theme.icon.remove
            onTriggered: removalDialog.show([page.view.slug], page.view.title, false)
        }
    }

    LibraryRemovalDialog {
        id: removalDialog
        model: page.ApplicationWindow.window ? page.ApplicationWindow.window.app.library : null
    }

    ModalDialog {
        id: autoDownloadDialog
        width: 480
        title: "Descargar episodios nuevos"
        onOpened: autoAcceptButton.forceActiveFocus(InputModality.reason)

        // Fixed widths on both sides: Fluent's Dialog loops when its content follows it.
        Label {
            width: 432
            wrapMode: Text.Wrap
            color: Theme.ink
            text: "Cuando salga un episodio nuevo de " + page.view.title
                  + ", AnimeHub lo descargará en " + page.view.downloadFolder
                  + ". Puedes dejar de hacerlo en este mismo menú."
        }

        // Windows order: primary action on the left, Cancel on the right.
        footer: DialogButtonBox {
            AccentButton {
                id: autoAcceptButton
                text: "Activar"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Accessible.onPressAction: click()
                Keys.onReturnPressed: event => { event.accepted = true; click() }
                Keys.onEnterPressed: event => { event.accepted = true; click() }
            }
        }
        onAccepted: page.view.setAutoDownload(true)
    }

    SelectionBar {
        id: selectionBar
        z: 15
        anchors.horizontalCenter: parent.horizontalCenter
        // Rises from the bottom edge.
        y: page.selecting ? page.height - height - 16 : page.height + 8
        opacity: page.selecting ? 1 : 0
        visible: opacity > 0
        Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200 } }
        view: page.view
        available: page.width - 32
        onCleared: page.clearSelection()
    }
}
