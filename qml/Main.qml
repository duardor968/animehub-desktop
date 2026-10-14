import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

ApplicationWindow {
    id: window

    required property AppController app

    function showPage(index) {
        tabs.currentIndex = index
    }

    width: app.settings.value("window/width", 1280)
    height: app.settings.value("window/height", 820)
    minimumWidth: 820
    minimumHeight: 600
    visible: true
    title: "AnimeHub"
    color: Theme.canvas
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.window: Theme.canvas

    Component.onCompleted: {
        // INI settings come back as text.
        if (String(app.settings.value("window/maximized", false)) === "true")
            showMaximized()
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
            window.show()
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
                PageTab { text: "Biblioteca" }
                PageTab { text: "Catálogo" }
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
            app: window.app
            onBrowseCatalog: window.showPage(1)
        }

        CatalogPage {
            app: window.app
            active: StackLayout.isCurrentItem
        }

        DownloadsPage {
            app: window.app
            onBrowseCatalog: window.showPage(1)
            onOpenAnime: slug => {
                window.showPage(0)
                libraryPage.select(slug)
            }
        }
    }

    footer: DownloadBar {
        downloads: window.app.downloads
        visible: window.app.downloads.activeCount > 0 && tabs.currentIndex !== 2
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
        target: window.app.libraryAnime
        function onMessage(text, problem) { toast.show(text, problem) }
    }
    Connections {
        target: window.app.catalogAnime
        function onMessage(text, problem) { toast.show(text, problem) }
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
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.app.quit() }
}
