import QtQuick
import AnimeHub

// An icon from the system icon font (hex codepoint like "e713") or a crisp vector icon served
// by GlyphProvider ("pot-play", "pot-pause", "pot-stop", "bookmark-ribbon").
Item {
    id: root

    property string glyph
    property int size: 16
    property color color: Theme.ink

    readonly property bool isCodepoint: /^[0-9a-fA-F]{4}$/.test(glyph)
    readonly property string hexRgb: {
        const s = color.toString()
        // Qt color.toString() is "#rrggbb" or "#aarrggbb"
        return s.length === 9 ? s.slice(3) : (s.length === 7 ? s.slice(1) : "ffffff")
    }

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Text {
        anchors.centerIn: parent
        visible: root.isCodepoint
        text: root.isCodepoint ? Theme.glyph(root.glyph) : ""
        color: root.color
        font.family: Theme.iconFont
        font.pixelSize: root.size
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        Accessible.ignored: true
    }

    Image {
        anchors.centerIn: parent
        visible: !root.isCodepoint && root.glyph !== ""
        width: root.size
        height: root.size
        source: visible ? Theme.source(root.glyph + "?color=" + root.hexRgb) : ""
        sourceSize.width: Math.max(16, root.size * 2)
        sourceSize.height: Math.max(16, root.size * 2)
        opacity: root.color.a
        smooth: true
        Accessible.ignored: true
    }
}
