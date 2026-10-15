import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// A section of the Downloads page as in Steam: its name, how many it holds, and a hairline to
// the edge, with the section's own actions at the end.
RowLayout {
    id: root

    property string text
    property int count
    default property alias actions: actionRow.data

    spacing: 12

    Label {
        text: root.text
        color: Theme.ink
        font.pixelSize: Theme.subtitle
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
        Accessible.name: root.text + " (" + root.count + ")"
    }

    Label {
        text: "(" + root.count + ")"
        color: Theme.faint
        font.pixelSize: Theme.subtitle
        Accessible.ignored: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        implicitHeight: 1
        color: Theme.stroke
    }

    Row {
        id: actionRow
        visible: children.length > 0
        spacing: 4
    }
}
