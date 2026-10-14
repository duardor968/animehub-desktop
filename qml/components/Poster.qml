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

    Image {
        id: image
        anchors.fill: parent
        source: root.source
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        sourceSize.width: Math.ceil(root.width * Screen.devicePixelRatio)
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    Glyph {
        anchors.centerIn: parent
        visible: image.status !== Image.Ready && image.status !== Image.Loading
        glyph: Theme.icon.video
        size: Math.min(root.width, root.height) / 4
        color: Theme.faint
    }
}
