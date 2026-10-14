import QtQuick
import AnimeHub

// An icon from the system icon font; `glyph` is its code (see Theme.icon).
Text {
    property string glyph
    property int size: 16

    text: Theme.glyph(glyph)
    color: Theme.ink
    font.family: Theme.iconFont
    font.pixelSize: size
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
    Accessible.ignored: true
}
