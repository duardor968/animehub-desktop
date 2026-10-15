import QtQuick
import QtQuick.Controls
import AnimeHub

// A menu item laid out like WinUI's MenuFlyout: its icon, or its check mark, in one column and
// every text in the next, whatever the item carries.
MenuItem {
    id: entry

    // Segoe Fluent Icons code of its icon, if any.
    property string glyph

    // Qt aligns the text of a menu's items to the widest padding; this one leaves the icon column.
    implicitTextPadding: 16 + spacing

    indicator: Glyph {
        x: entry.mirrored ? entry.width - width - entry.rightPadding : entry.leftPadding
        y: entry.topPadding + (entry.availableHeight - height) / 2
        width: 16
        height: 16
        visible: entry.glyph !== "" || entry.checked
        glyph: entry.glyph !== "" ? entry.glyph : Theme.icon.check
        color: entry.palette.text
    }
}
