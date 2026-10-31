import QtQuick
import AnimeHub

// Total download speed of the last minutes as bars, newest on the right, scaled to the highest
// one on view. As Steam's, the bars stand on the header's own backdrop, over a faint axis: no
// plot area of its own, so at rest only the axis is left.
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

    implicitWidth: 240
    implicitHeight: 88
    Accessible.ignored: true

    Repeater {
        model: root.bars

        Rectangle {
            required property int index
            readonly property real value: root.samples[root.samples.length - root.bars + index]

            x: root.width - (root.bars - index) * root.pitch + 1
            anchors.bottom: axis.top
            width: root.pitch - 1
            visible: value > 0
            height: root.highest > 0 ? Math.max(2, Math.round(value / root.highest * (root.height - 1))) : 0
            // Lighter towards the top, so the bars melt into the art behind them.
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55) }
                GradientStop { position: 1.0; color: Theme.accent }
            }
        }
    }

    Rectangle {
        id: axis
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
        opacity: 0.6
    }
}
