import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

ApplicationWindow {
    id: window

    required property AppController app
    property bool smokeHidden: false

    function showPage(index) {
        tabs.currentIndex = index
    }

    // Where the user has been, for ← and → as in Steam: the tab and what it showed. A new place
    // drops the places ahead of the current one, as a browser does.
    property var history: []
    property int historyIndex: -1
    property bool travelling: false
    // What had the keyboard in each place when it was left, to give it back on returning:
    // {item, reason} by history index.
    property var focusAt: ({})
    // What had it when something opened a new place, until that place is recorded.
    property var leaving: null
    function here() {
        return { page: tabs.currentIndex, library: libraryPage.location(),
                 catalog: catalogPage.depth > 1 ? app.catalogAnime.slug : "",
                 catalogFrom: catalogPage.from }
    }
    function remember() {
        if (travelling)
            return
        const now = here()
        if (historyIndex >= 0 && JSON.stringify(history[historyIndex]) === JSON.stringify(now))
            return
        if (leaving && historyIndex >= 0)
            focusAt[historyIndex] = leaving
        leaving = null
        history = history.slice(0, historyIndex + 1).concat([now])
        historyIndex = history.length - 1
    }
    function travel(step) {
        const index = historyIndex + step
        if (index < 0 || index >= history.length)
            return
        const reason = openingReason()
        focusAt[historyIndex] = { item: activeFocusItem, reason: reason }
        travelling = true
        const place = history[index]
        tabs.currentIndex = place.page
        libraryPage.restore(place.library)
        if (place.catalog)
            catalogPage.openAnime(place.catalog, place.catalogFrom, true)
        else
            catalogPage.home()
        historyIndex = index
        travelling = false
        arrive(index, reason)
    }
    // The keyboard back where it was in that place; else on the title of the page there.
    function arrive(index, reason) {
        const last = focusAt[index]
        try {
            if (last && last.item && last.item.visible) {
                last.item.forceActiveFocus(last.reason)
                return
            }
        } catch (gone) {
            // The item went with its page.
        }
        if (tabs.currentIndex === 0)
            libraryPage.focusTitle(reason)
        else if (tabs.currentIndex === 1)
            catalogPage.focusTitle(reason)
    }
    // Esc: back where the user was; at the first place, up to the tab's start.
    function goBack() {
        if (historyIndex > 0)
            travel(-1)
        else if (tabs.currentIndex === 0)
            libraryPage.select("")
        else if (tabs.currentIndex === 1)
            catalogPage.home()
    }
    // How a place is being opened, and what had the keyboard: a mouse-led opening shows no focus
    // frame on the new page, as Fluent's controls show none for a click.
    function openingReason() {
        const item = activeFocusItem
        const reason = item && item.focusReason === Qt.MouseFocusReason ? Qt.MouseFocusReason
                                                                         : Qt.OtherFocusReason
        if (!travelling && item)
            leaving = { item: item, reason: item.focusReason ?? Qt.OtherFocusReason }
        return reason
    }
    // Places change after the click that changes them: one record per move, not per property.
    Timer {
        id: rememberLater
        interval: 0
        onTriggered: window.remember()
    }

    // An anime's page: in the library if it is there, otherwise in the catalog. Opened from the
    // Downloads tab, its breadcrumb leads back there.
    function openAnime(slug, from) {
        const downloads = from === "downloads"
        if (app.library.contains(slug)) {
            showPage(0)
            libraryPage.select(slug, downloads ? "downloads" : "library")
        } else {
            showPage(1)
            catalogPage.openAnime(slug, downloads ? "downloads" : "catalog")
        }
    }

    function searchCatalog(text) {
        showPage(1)
        catalogPage.search(text)
    }

    function focusSearch() {
        if (tabs.currentIndex === 1) {
            catalogPage.focusSearch()
        } else {
            showPage(0)
            libraryPage.focusSearch()
        }
    }

    width: app.settings.value("window/width", 1280)
    height: app.settings.value("window/height", 820)
    minimumWidth: 820
    minimumHeight: 600
    // main() supplies visibility; construction itself never exposes a native window.
    visible: false
    title: "AnimeHub"
    color: Theme.canvas
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.window: Theme.canvas

    // Open modal dialogs. Their dimming covers the content, not the native title bar, which
    // is darkened by the same amount (Fluent dims with 30 % black per dialog).
    property int modalLayers: 0
    onModalLayersChanged: tintTitleBar()
    function tintTitleBar() {
        let caption = Theme.pane
        let text = Theme.ink
        for (let i = 0; i < modalLayers; ++i) {
            caption = Qt.tint(caption, Qt.rgba(0, 0, 0, 0.3))
            text = Qt.tint(text, Qt.rgba(0, 0, 0, 0.3))
        }
        app.tintTitleBar(window, caption, text)
    }

    // INI settings come back as text.
    function showAsLeft() {
        if (smokeHidden)
            return
        if (String(app.settings.value("window/maximized", false)) === "true")
            showMaximized()
        else
            show()
    }

    Component.onCompleted: {
        // It opens on the page it was left on.
        const page = Number(app.settings.value("window/page", 0))
        tabs.currentIndex = page >= 0 && page <= 2 ? page : 0
        rememberLater.restart()
        tintTitleBar()
        // Started in the tray, it stays hidden until asked for.
        if (visible)
            showAsLeft()
    }

    onClosing: close => {
        close.accepted = false
        app.settings.setValue("window/maximized", visibility === Window.Maximized)
        if (visibility !== Window.Maximized) {
            app.settings.setValue("window/width", width)
            app.settings.setValue("window/height", height)
        }
        app.requestClose()
    }

    Connections {
        target: window.app
        function onShowRequested() {
            if (window.smokeHidden)
                return
            if (!window.visible)
                window.showAsLeft()
            window.raise()
            window.requestActivate()
        }
        function onHideRequested() { window.hide() }
        function onCloseDecisionNeeded() { closeDialog.open() }
        function onAnimeRequested(slug) {
            window.showPage(0)
            libraryPage.select(slug)
        }
    }

    header: Rectangle {
        implicitHeight: 48
        color: Theme.pane

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 8

            ToolButton {
                text: "Atrás"
                display: AbstractButton.IconOnly
                icon.source: Theme.source(Theme.icon.back)
                enabled: window.historyIndex > 0
                onClicked: window.travel(-1)
                ToolTip.visible: hovered
                ToolTip.text: "Atrás (Alt+←)"
            }

            ToolButton {
                text: "Adelante"
                display: AbstractButton.IconOnly
                icon.source: Theme.source("e72a")
                enabled: window.historyIndex < window.history.length - 1
                onClicked: window.travel(1)
                ToolTip.visible: hovered
                ToolTip.text: "Adelante (Alt+→)"
            }

            TabBar {
                id: tabs
                Layout.alignment: Qt.AlignBottom
                background: null
                onCurrentIndexChanged: {
                    window.app.settings.setValue("window/page", currentIndex)
                    rememberLater.restart()
                }
                // The tab of the page on view takes that page back to its start, as in Steam.
                PageTab {
                    text: "Biblioteca"
                    onReselected: libraryPage.select("")
                }
                PageTab {
                    text: "Catálogo"
                    onReselected: catalogPage.home()
                }
                PageTab { text: "Descargas" }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                icon.source: Theme.source(Theme.icon.settings)
                display: AbstractButton.IconOnly
                text: "Ajustes"
                ToolTip.visible: hovered
                ToolTip.text: "Ajustes (Ctrl+,)"
                onClicked: settingsDialog.open()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.stroke
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex

        LibraryPage {
            id: libraryPage
            objectName: "libraryPage"
            app: window.app
            onActionFailed: text => toast.show(text, true)
            onBrowseCatalog: window.showPage(1)
            onSearchCatalog: text => window.searchCatalog(text)
            onBack: window.goBack()
            onShowDownloads: window.showPage(2)
            onSelectedChanged: rememberLater.restart()
            onSelectedGroupChanged: rememberLater.restart()
            onShowGridChanged: rememberLater.restart()
        }

        CatalogPage {
            id: catalogPage
            app: window.app
            active: StackLayout.isCurrentItem
            onDepthChanged: rememberLater.restart()
            onBack: window.goBack()
            onShowDownloads: window.showPage(2)
        }

        DownloadsPage {
            app: window.app
            onBrowseCatalog: window.showPage(1)
            onOpenAnime: slug => window.openAnime(slug, "downloads")
        }
    }

    // The mouse's side buttons go back and forward, as in Explorer; other buttons pass through.
    MouseArea {
        anchors.fill: parent
        z: 100
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onClicked: mouse => window.travel(mouse.button === Qt.BackButton ? -1 : 1)
    }

    footer: DownloadBar {
        downloads: window.app.downloads
        visible: window.app.downloads.barVisible && tabs.currentIndex !== 2
        onClicked: window.showPage(2)
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 10
    }

    Connections {
        target: window.app
        function onMessage(text, problem) { toast.show(text, problem) }
    }
    Connections {
        target: window.app.libraryAnime
        function onMessage(text, problem) { toast.show(text, problem) }
    }
    Connections {
        target: window.app.catalogAnime
        function onMessage(text, problem) { toast.show(text, problem) }
        function onSlugChanged() { rememberLater.restart() }
    }
    Connections {
        target: window.app.libraryAnime
        function onUndoable(text) { toast.show(text, false, "Deshacer", () => window.app.libraryAnime.undo()) }
    }
    Connections {
        target: window.app.catalogAnime
        function onUndoable(text) { toast.show(text, false, "Deshacer", () => window.app.catalogAnime.undo()) }
    }
    // Whatever adds an anime (watching it, downloading it, a star) says so, with the way to it.
    Connections {
        target: window.app.libraryAnime
        function onAddedToLibrary(slug, title) {
            toast.show("Añadido a la biblioteca", false, "Ver", () => window.openAnime(slug))
        }
    }
    Connections {
        target: window.app.catalogAnime
        function onAddedToLibrary(slug, title) {
            toast.show("Añadido a la biblioteca", false, "Ver", () => window.openAnime(slug))
        }
    }
    Connections {
        target: window.app.downloads
        function onMessage(text, problem) { toast.show(text, problem) }
    }

    SettingsDialog {
        id: settingsDialog
        app: window.app
    }

    CloseDialog {
        id: closeDialog
        app: window.app
    }

    Shortcut { sequence: "Alt+Left"; onActivated: window.travel(-1) }
    Shortcut { sequence: "Alt+Right"; onActivated: window.travel(1) }
    Shortcut { sequence: "Ctrl+1"; onActivated: window.showPage(0) }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.showPage(1) }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.showPage(2) }
    Shortcut { sequences: [StandardKey.Find]; onActivated: window.focusSearch() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.app.quit() }
}
