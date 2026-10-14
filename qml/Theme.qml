pragma Singleton
import QtQuick

// Design tokens: one dark palette, one spacing scale, one type scale.
QtObject {
    readonly property color canvas: "#111317"
    readonly property color pane: "#16191e"
    readonly property color raised: "#1d2127"
    readonly property color hover: "#232830"
    readonly property color selected: "#1f2a3a"
    readonly property color stroke: "#2a2f37"
    readonly property color ink: "#e9ebee"
    readonly property color muted: "#9aa1ab"
    readonly property color faint: "#6c737e"
    readonly property color accent: "#2f81f7"
    readonly property color accentInk: "#ffffff"
    readonly property color success: "#4cb782"
    readonly property color warning: "#d8a23a"
    readonly property color danger: "#e5534b"

    readonly property int gap: 8
    readonly property int pad: 16
    readonly property int section: 32
    readonly property int radius: 6

    readonly property int caption: 12
    readonly property int body: 14
    readonly property int subtitle: 18
    readonly property int title: 28

    readonly property string iconFont: "Segoe Fluent Icons"

    // A Segoe Fluent Icons glyph (the system icon font of Windows 11) as text, or as an image
    // for a control's `icon.source`.
    function glyph(code) { return String.fromCharCode(parseInt(code, 16)) }
    function source(code) { return "image://glyph/" + code }

    // Codes of the glyphs AnimeHub uses.
    readonly property QtObject icon: QtObject {
        readonly property string library: "e8f1"
        readonly property string search: "e721"
        readonly property string download: "e896"
        readonly property string settings: "e713"
        readonly property string play: "e768"
        readonly property string pause: "e769"
        readonly property string cancel: "e711"
        readonly property string more: "e712"
        readonly property string star: "e734"
        readonly property string starFilled: "e735"
        readonly property string folder: "e8b7"
        readonly property string check: "e73e"
        readonly property string retry: "e72c"
        readonly property string back: "e72b"
        readonly property string add: "e710"
        readonly property string remove: "e738"
        readonly property string bell: "ea8f"
        readonly property string warning: "e7ba"
        readonly property string clock: "e823"
        readonly property string video: "e714"
        readonly property string send: "e724"
        readonly property string trash: "e74d"
        readonly property string info: "e946"
        readonly property string filter: "e71c"
    }
}
