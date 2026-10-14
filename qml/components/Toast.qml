import QtQuick
import QtQuick.Controls
import AnimeHub

// A short outcome at the bottom of the window ("Enlaces enviados a JDownloader").
Rectangle {
    id: root

    function show(text, problem) {
        label.text = text
        root.problem = problem
        opacity = 1
        timer.restart()
    }

    property bool problem: false

    width: Math.min(row.implicitWidth + 32, parent.width - 48)
    height: row.implicitHeight + 20
    radius: Theme.radius
    color: Theme.raised
    border.color: problem ? Theme.danger : Theme.stroke
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 180 } }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: label.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10

        Glyph {
            glyph: root.problem ? Theme.icon.warning : Theme.icon.check
            color: root.problem ? Theme.danger : Theme.success
            anchors.verticalCenter: parent.verticalCenter
        }

        Label {
            id: label
            color: Theme.ink
            font.pixelSize: Theme.body
            width: Math.min(implicitWidth, root.parent ? root.parent.width - 120 : 600)
            wrapMode: Text.Wrap
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Timer {
        id: timer
        interval: root.problem ? 6000 : 3500
        onTriggered: root.opacity = 0
    }
}
