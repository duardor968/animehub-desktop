import QtQuick
import AnimeHub

// A cover image with a quiet placeholder while it loads or when there is none.
Rectangle {
    id: root

    property url source
    property bool rounded: true

    color: Theme.raised
    radius: rounded ? Theme.radius : 0
    clip: true
    onSourceChanged: image.retries = 0

    Image {
        id: image
        property int retries: 0
        anchors.fill: parent
        source: root.source
        // A transient server error should not leave an empty poster for the whole session.
        onStatusChanged: if (status === Image.Error && retries < 3) retry.start()
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        // Both sides: cropped, a wide image is decoded just large enough to cover, not to the width.
        sourceSize.width: Math.ceil(root.width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(root.height * Screen.devicePixelRatio)
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    Timer {
        id: retry
        interval: 4000 * (image.retries + 1)
        onTriggered: {
            image.retries += 1
            image.source = ""
            image.source = Qt.binding(() => root.source)
        }
    }

    Glyph {
        anchors.centerIn: parent
        visible: image.status !== Image.Ready && image.status !== Image.Loading
        glyph: Theme.icon.video
        size: Math.min(root.width, root.height) / 4
        color: Theme.faint
    }
}
