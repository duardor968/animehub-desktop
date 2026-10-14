import QtQuick
import QtQuick.Controls
import AnimeHub

// A menu that lists, each time it opens, only the entries that apply: Fluent spaces its items
// 4 px apart, so a hidden entry would still leave a gap. Entries say whether they apply in a
// `wanted` property; separators (`separator: true`) stay only between kept entries.
ActionMenu {
    id: menu

    // Every entry, in order.
    property list<QtObject> entries

    function show(anchor) {
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
        popup(anchor, 0, anchor.height)
    }
}
