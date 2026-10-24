import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One setting as in Windows' and Steam's settings: its name on the left, with an ⓘ when the
// control alone cannot show a consequence; its value and its controls against the right edge,
// the same edge on every row; a hairline under it. In a narrow window they go under the name.
Item {
    id: row

    property string label
    property string info
    // The current value as text, before the controls that change it.
    property string value
    property color valueColor: Theme.ink
    // Why its controls cannot be used now, told over them on hover; empty while they can.
    property string unavailable
    default property alias content: slot.data

    // What a name keeps beside a long value.
    readonly property int nameWidth: 180
    readonly property bool stacked: width < 520
    // Rows taller than one control line keep the name level with their first line.
    readonly property bool tall: end.implicitHeight > 40
    readonly property real nameLine: title.height / Math.max(1, title.lineCount)

    Layout.fillWidth: true
    implicitHeight: stacked ? name.height + end.implicitHeight + 30
                            : Math.max(52, end.implicitHeight + 20, name.height + 20)

    Item {
        id: name

        width: row.stacked ? row.width : row.width - end.width - 24
        height: title.height
        y: row.stacked ? 12 : row.tall ? 10 + (32 - row.nameLine) / 2 : (row.height - height) / 2

        Label {
            id: title
            width: Math.min(implicitWidth, name.width - (mark.visible ? mark.width + 6 : 0))
            text: row.label
            color: Theme.ink
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Glyph {
            id: mark
            x: title.contentWidth + 6
            y: (row.nameLine - height) / 2
            visible: row.info !== ""
            glyph: Theme.icon.info
            color: Theme.muted
            HoverHandler { id: hover }
            ToolTip.visible: hover.hovered
            ToolTip.text: row.info
        }
    }

    RowLayout {
        id: end

        // A long value gives way down to the room its name keeps.
        readonly property real room: row.width - 24
                                     - Math.min(row.nameWidth, title.implicitWidth
                                                + (mark.visible ? mark.width + 6 : 0))

        width: row.stacked ? row.width : Math.min(implicitWidth, room)
        x: row.width - width
        y: row.stacked ? name.y + name.height + 6 : row.tall ? 10 : (row.height - height) / 2
        spacing: 0

        Label {
            id: shown
            Layout.fillWidth: true
            // Whole pixels: the layout rounds, and a fraction short would elide it.
            Layout.preferredWidth: Math.ceil(implicitWidth)
            Layout.maximumWidth: Math.ceil(implicitWidth)
            // A value alone ends at the edge too.
            Layout.rightMargin: slot.implicitWidth > 0 ? 12 : 0
            visible: row.value !== ""
            text: row.value
            color: row.valueColor
            elide: Text.ElideMiddle
            ToolTip.visible: valueHover.hovered && truncated
            ToolTip.text: text
            HoverHandler { id: valueHover }
        }
        RowLayout {
            id: slot
            Layout.fillWidth: true
            Layout.preferredWidth: Math.ceil(implicitWidth)
            Layout.maximumWidth: Math.ceil(implicitWidth)
            enabled: row.unavailable === ""
            spacing: 8
        }
        // Under the name, the value and the controls start at the left.
        Item { Layout.fillWidth: true }
    }

    // A disabled control gets no hover of its own: the reason shows over it.
    Item {
        x: end.x + slot.x
        y: end.y + slot.y
        width: slot.width
        height: slot.height
        visible: row.unavailable !== ""
        HoverHandler { id: reasonHover }
        ToolTip.visible: reasonHover.hovered
        ToolTip.text: row.unavailable
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }
}
