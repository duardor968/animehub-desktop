import QtQuick
import QtQuick.Controls

// A centred modal dialog. Its dimming layer cannot reach the native title bar, so the window
// darkens that bar too while the dialog is open.
Dialog {
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    onAboutToShow: ApplicationWindow.window.modalLayers += 1
    onAboutToHide: ApplicationWindow.window.modalLayers -= 1
}
