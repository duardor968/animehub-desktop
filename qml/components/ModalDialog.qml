import QtQuick
import QtQuick.Controls
import AnimeHub

// A centred modal dialog. Its dimming layer cannot reach the native title bar, so the window
// darkens that bar too while the dialog is open.
Dialog {
    id: dialog

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    onAboutToShow: if (ApplicationWindow.window && ApplicationWindow.window.modalLayers !== undefined)
                       ApplicationWindow.window.modalLayers += 1
    onAboutToHide: if (ApplicationWindow.window && ApplicationWindow.window.modalLayers !== undefined)
                       ApplicationWindow.window.modalLayers -= 1

    // Wrapping Fluent title header (FluentWinUI3's default elides long titles in one line).
    // Callers with a custom layout (SettingsDialog, JDownloaderSetup) override `header`.
    header: Label {
        visible: dialog.visible && dialog.title !== ""
        text: dialog.title
        topPadding: dialog.padding
        leftPadding: dialog.padding
        rightPadding: dialog.padding
        wrapMode: Text.Wrap
        color: Theme.ink
        font.pixelSize: Theme.heading
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
    }
}
