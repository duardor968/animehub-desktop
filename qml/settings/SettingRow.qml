import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One setting as in Steam's settings: its name, an ⓘ when the control alone cannot show a
// consequence, the control, and a hairline under the row. Names share one column on every page;
// in a narrow window the control goes under its name, as Windows' settings do.
Item {
    id: row

    property string label
    property string info
    default property alias content: slot.data

    readonly property int nameWidth: 180
    readonly property bool stacked: width < 520
    // Rows taller than one control line keep the name level with their first line.
    readonly property bool tall: slot.implicitHeight > 40

    Layout.fillWidth: true
    implicitHeight: stacked ? name.height + slot.implicitHeight + 30
                            : Math.max(52, slot.implicitHeight + 20)

    RowLayout {
        id: name

        width: row.stacked ? row.width : row.nameWidth
        y: row.stacked ? 12 : row.tall ? 10 + (32 - height) / 2 : (row.height - height) / 2
        spacing: 6

        Label {
            Layout.maximumWidth: name.width - 22
            text: row.label
            color: Theme.ink
            elide: Text.ElideRight
        }
        Glyph {
            visible: row.info !== ""
            glyph: Theme.icon.info
            color: Theme.muted
            HoverHandler { id: hover }
            ToolTip.visible: hover.hovered
            ToolTip.text: row.info
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout {
        id: slot

        x: row.stacked ? 0 : row.nameWidth + 24
        width: row.width - x
        y: row.stacked ? name.y + name.height + 6 : row.tall ? 10 : (row.height - height) / 2
        spacing: 8
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }
}
