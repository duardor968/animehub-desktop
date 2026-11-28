pragma Singleton
import QtQuick

// Design tokens: one dark palette, one spacing scale, one type scale.
QtObject {
    readonly property color canvas: "#111317"
    readonly property color pane: "#16191e"
    readonly property color surface: "#151922"
    readonly property color raised: "#1d2127"
    readonly property color hover: "#232830"
    readonly property color stroke: "#2a2f37"
    readonly property color ink: "#e9ebee"
    readonly property color muted: "#9aa1ab"
    readonly property color faint: "#6c737e"
    readonly property color accent: "#2f81f7"
    // Higher-luminance blue for readable text/chips on dark surfaces (WCAG AA >= 6.8:1).
    readonly property color accentText: "#6ea8ff"
    readonly property color accentInk: "#ffffff"
    // Fluent's own accent for a disabled control (AccentFillColorDisabled): the brand accent
    // replaces Fluent's in every state, so this one is given back.
    readonly property color accentDisabled: "#28ffffff"
    // Disabled neutral control fill (distinct from accentDisabled for emphasized accent buttons).
    readonly property color controlDisabled: "#1b1f26"
    readonly property color hoverSubtle: Qt.rgba(1, 1, 1, 0.05)
    readonly property color pressSubtle: Qt.rgba(1, 1, 1, 0.08)
    readonly property color success: "#4cb782"
    readonly property color warning: "#d8a23a"
    readonly property color danger: "#e5534b"
    readonly property color limitLine: "#ff9d8f"

    readonly property int pad: 16
    readonly property int section: 32
    readonly property int radiusSm: 4
    readonly property int radius: 6
    readonly property int radiusLg: 8

    // Posters keep near one width in every row and grid: the columns change, not the size.
    readonly property int posterWidth: 150
    readonly property int posterGap: 16
    function posterColumns(width) {
        return Math.max(1, Math.round((width + posterGap) / (posterWidth + posterGap)))
    }
    // A poster card: the cover, its title, a caption and the gap under them.
    // The poster, two lines of title and the caption under them.
    function posterCardHeight(cardWidth) { return Math.round(cardWidth * 1.42) + 90 }

    readonly property int micro: 11
    readonly property int caption: 12
    readonly property int small: 13
    readonly property int body: 14
    readonly property int subheading: 15
    readonly property int subtitle: 18
    readonly property int heading: 20
    readonly property int title: 28

    readonly property int tooltipDelay: 450
    readonly property int tooltipTimeout: 5000

    // Segoe Fluent Icons is Windows' own; elsewhere Uno Fluent Icons (family "Symbols") draws the
    // same codes.
    readonly property string iconFont: Qt.platform.os === "windows" ? "Segoe Fluent Icons"
                                                                    : "Symbols"

    // A Segoe Fluent Icons glyph (the system icon font of Windows 11) as text, or as an image
    // for a control's `icon.source`.
    function glyph(code) { return String.fromCharCode(parseInt(code, 16)) }
    function source(code) { return "image://glyph/" + code }

    // Codes of the glyphs AnimeHub uses. Transport glyphs (play/pause) and the vertical
    // bookmark ribbon are crisp anti-aliased QPainterPath vectors served by GlyphProvider so
    // Windows and Linux match identically at every DPI.
    readonly property QtObject icon: QtObject {
        readonly property string library: "e8f1"
        readonly property string home: "e80f"
        readonly property string grid: "f0e2"
        readonly property string list: "e8fd"
        readonly property string download: "e896"
        readonly property string settings: "e713"
        readonly property string play: "pot-play"
        readonly property string pause: "pot-pause"
        readonly property string bookmarkRibbon: "bookmark-ribbon"
        readonly property string cancel: "e711"
        readonly property string more: "e712"
        readonly property string star: "e734"
        readonly property string starFilled: "e735"
        readonly property string folder: "e8b7"
        readonly property string check: "e73e"
        readonly property string retry: "e72c"
        readonly property string add: "e710"
        readonly property string remove: "e738"
        readonly property string bell: "ea8f"
        readonly property string bellOff: "e7ed"
        readonly property string warning: "e7ba"
        readonly property string clock: "e823"
        readonly property string video: "e714"
        readonly property string info: "e946"
        readonly property string copy: "e8c8"
        readonly property string openExternal: "e8a7"
        readonly property string schedule: "e787"
        readonly property string search: "e721"
        readonly property string filter: "e71c"
        readonly property string trash: "e74d"
        readonly property string edit: "e70f"
        readonly property string archive: "e7b8"
        readonly property string undo: "e7a7"
        readonly property string multiSelect: "e762"
        readonly property string chevronLeft: "e76b"
        readonly property string chevronRight: "e76c"
        readonly property string chevronDown: "e70d"
        readonly property string chevronUp: "e70e"
        readonly property string collapseAll: "e70e"
        readonly property string expandAll: "e70d"
        readonly property string update: "e777"
    }
}
