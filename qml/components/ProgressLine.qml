import QtQuick
import AnimeHub

// A thin progress line: watched part of an anime or of an episode, or a download.
Rectangle {
    property real value: 0

    implicitHeight: 3
    radius: height / 2
    color: Theme.stroke

    Rectangle {
        width: parent.width * Math.max(0, Math.min(1, parent.value))
        height: parent.height
        radius: parent.radius
        color: Theme.accent
    }
}
