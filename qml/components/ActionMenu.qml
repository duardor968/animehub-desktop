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

    // Only what changes moves: taking an entry out and adding it back while the menu shows
    // leaves the menu's list a stale view of it, and hovering the entry then hides it.
    function rebuild() {
        const wanted = []
        for (const entry of entries) {
            if (entry.wanted === false)
                continue
            const last = wanted.length > 0 ? wanted[wanted.length - 1] : null
            if (entry.separator === true && (last === null || last.separator === true))
                continue
            wanted.push(entry)
        }
        if (wanted.length > 0 && wanted[wanted.length - 1].separator === true)
            wanted.pop()
        for (let i = count - 1; i >= 0; --i) {
            if (!wanted.includes(itemAt(i)))
                takeItem(i)
        }
        wanted.forEach((entry, index) => {
            if (itemAt(index) !== entry)
                insertItem(index, entry)
        })
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
