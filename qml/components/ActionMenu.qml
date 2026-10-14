import QtQuick
import QtQuick.Controls

// A Fluent menu as wide as its longest visible entry; Qt's sizes every menu to 200 px.
Menu {
    id: menu

    onAboutToShow: {
        let widest = implicitWidth - leftPadding - rightPadding
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item && item.visible)
                widest = Math.max(widest, item.implicitWidth)
        }
        width = widest + leftPadding + rightPadding
    }
}
