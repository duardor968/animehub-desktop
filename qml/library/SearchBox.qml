import QtQuick
import QtQuick.Controls
import AnimeHub

// Fluent's search field, with the clear button. Qt's style draws no placeholder for it: the
// label sits where TextField draws its own, in the same colour.
SearchField {
    id: root

    property string placeholderText

    Accessible.name: placeholderText

    Label {
        x: root.contentItem.x + root.contentItem.leftPadding
        y: root.contentItem.y + root.contentItem.topPadding
        width: root.contentItem.width - root.contentItem.leftPadding - root.contentItem.rightPadding
        height: root.contentItem.height - root.contentItem.topPadding - root.contentItem.bottomPadding
        visible: root.text === ""
        text: root.placeholderText
        color: root.palette.placeholderText
        font: root.font
        verticalAlignment: root.contentItem.verticalAlignment
        elide: Text.ElideRight
        Accessible.ignored: true
    }
}
