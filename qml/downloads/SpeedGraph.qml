import QtQuick
import AnimeHub

// Total download speed of the last minutes as bars, newest on the right, scaled to the highest
// one on view. As Steam's, its area and its axis stay at rest: the bars rise inside them.
Item {
    id: root

    // Bytes per second, oldest first.
    property list<real> samples
    readonly property int pitch: 4
    readonly property int bars: Math.max(0, Math.min(samples.length, Math.floor((width + 1) / pitch)))
    readonly property real highest: {
        let found = 0
        for (let i = samples.length - bars; i < samples.length; ++i)
            found = Math.max(found, samples[i])
        return found
    }
    // Room over the highest bar, under the top of the area.
    readonly property real span: height - 8

    implicitWidth: 240
    implicitHeight: 88
    Accessible.ignored: true

    // The plot area, as in Task Manager's graphs.
    Rectangle {
        anchors.fill: parent
        color: Theme.canvas
    }

    // Half the highest speed on view.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.height - 1 - Math.round(root.span / 2)
        height: 1
        color: Theme.stroke
        opacity: 0.5
    }

    Repeater {
        model: root.bars

        Rectangle {
            required property int index
            readonly property real value: root.samples[root.samples.length - root.bars + index]

            x: root.width - (root.bars - index) * root.pitch + 1
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            width: root.pitch - 1
            visible: value > 0
            height: root.highest > 0 ? Math.max(3, Math.round(value / root.highest * root.span)) : 0
            color: Theme.accent
        }
    }

    // The axis.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }
}
