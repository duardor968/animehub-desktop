import QtQuick
import QtQuick.Controls
import AnimeHub

// A titled row of posters that scrolls sideways.
Column {
    id: root

    property alias model: list.model
    property string title
    property int cardWidth: 150

    signal activated(string slug, real episode)

    spacing: 12
    visible: list.count > 0

    Label {
        text: root.title
        color: Theme.ink
        font.pixelSize: Theme.subtitle
        font.weight: Font.DemiBold
    }

    ListView {
        id: list
        width: root.width
        // Poster, two title lines and a caption; fixed so the row exists before its delegates.
        height: Math.round(root.cardWidth * 1.42) + 92
        orientation: ListView.Horizontal
        spacing: 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        Accessible.name: root.title
        ScrollBar.horizontal: ScrollBar { policy: list.contentWidth > list.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        delegate: PosterCard {
            required property var model

            width: root.cardWidth
            text: model.title
            posterUrl: model.posterUrl
            caption: model.caption
            progress: model.progress
            fresh: model.fresh
            onClicked: root.activated(model.slug, model.episode)
        }
    }
}
