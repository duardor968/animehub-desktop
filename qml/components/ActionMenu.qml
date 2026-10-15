import QtQuick
import QtQuick.Controls

// A Fluent menu as wide as its widest entry; Qt sizes every menu to 200 px. With `entries`, it
// lists, each time it opens, only those that apply: Fluent spaces items 4 px apart, so a hidden
// entry would still leave a gap. Entries say whether they apply in a `wanted` property;
// separators (`separator: true`) stay only between kept entries.
Menu {
    id: menu

    // Every entry, in order.
    property list<QtObject> entries

    // Opens under `anchor`, left edges aligned, or right edges with `alignRight`.
    function show(anchor, alignRight) {
        if (entries.length > 0)
            rebuild()
        fit()
        // Fluent draws the menu past its box by its (negative) inset: the drawn edges align.
        popup(anchor, alignRight ? anchor.width - width + rightInset : 0, anchor.height)
    }

    function rebuild() {
        while (count > 0)
            takeItem(0)
        let last = null
        for (const entry of entries) {
            if (entry.wanted === false)
                continue
            if (entry.separator === true && (last === null || last.separator === true))
                continue
            addItem(entry)
            last = entry
        }
        if (last !== null && last.separator === true)
            takeItem(count - 1)
    }

    function fit() {
        let widest = implicitWidth - leftPadding - rightPadding
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item && item.visible)
                widest = Math.max(widest, item.implicitWidth)
        }
        width = widest + leftPadding + rightPadding
    }

    onAboutToShow: fit()
}
