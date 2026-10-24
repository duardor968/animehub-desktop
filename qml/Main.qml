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

    // An anime's page: in the library if it is there, otherwise in the catalog.
    function openAnime(slug) {
        if (app.library.contains(slug)) {
            showPage(0)
            libraryPage.select(slug)
        } else {
            showPage(1)
            catalogPage.openAnime(slug)
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

            TabBar {
                id: tabs
                Layout.alignment: Qt.AlignBottom
                background: null
                onCurrentIndexChanged: window.app.settings.setValue("window/page", currentIndex)
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
        }

        CatalogPage {
            id: catalogPage
            app: window.app
            active: StackLayout.isCurrentItem
        }

        DownloadsPage {
            app: window.app
            onBrowseCatalog: window.showPage(1)
            onOpenAnime: slug => window.openAnime(slug)
        }
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
    }
    Connections {
        target: window.app.libraryAnime
        function onUndoable(text) { toast.show(text, false, "Deshacer", () => window.app.libraryAnime.undo()) }
    }
    Connections {
        target: window.app.catalogAnime
        function onUndoable(text) { toast.show(text, false, "Deshacer", () => window.app.catalogAnime.undo()) }
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

    Shortcut { sequence: "Ctrl+1"; onActivated: window.showPage(0) }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.showPage(1) }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.showPage(2) }
    Shortcut { sequences: [StandardKey.Find]; onActivated: window.focusSearch() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.app.quit() }
}
