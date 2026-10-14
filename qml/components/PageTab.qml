import QtQuick
import QtQuick.Controls
import AnimeHub

// A page tab of the window. Fluent draws its text with `icon.color`.
TabButton {
    width: implicitWidth + 12
    icon.color: checked ? Theme.ink : Theme.muted
    font.pixelSize: Theme.body
    font.weight: checked ? Font.DemiBold : Font.Normal
    // Screen readers activate a tab with their "press" action.
    Accessible.onPressAction: click()
}
