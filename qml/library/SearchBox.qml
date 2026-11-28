import QtQuick
import QtQuick.Controls
import AnimeHub

// Fluent's native SearchField (Qt 6.12 draws its own placeholder and clear indicator), with
// accessible labels on the inner editor and clear button plus standardized tooltip timing.
SearchField {
    id: root

    Accessible.name: placeholderText
    // The text field inside is what takes the keyboard: it carries the name a reader announces.
    Binding {
        target: root.contentItem ? root.contentItem.Accessible : null
        property: "name"
        value: root.placeholderText
    }
    Binding {
        target: root.clearIndicator.indicator ? root.clearIndicator.indicator.Accessible : null
        property: "name"
        value: "Limpiar búsqueda"
    }
    Binding {
        target: root.clearIndicator.indicator ? root.clearIndicator.indicator.ToolTip : null
        property: "visible"
        value: root.clearIndicator.hovered && root.text.length > 0
    }
    Binding {
        target: root.clearIndicator.indicator ? root.clearIndicator.indicator.ToolTip : null
        property: "delay"
        value: Theme.tooltipDelay
    }
    Binding {
        target: root.clearIndicator.indicator ? root.clearIndicator.indicator.ToolTip : null
        property: "timeout"
        value: Theme.tooltipTimeout
    }
    Binding {
        target: root.clearIndicator.indicator ? root.clearIndicator.indicator.ToolTip : null
        property: "text"
        value: "Limpiar búsqueda"
    }
}
