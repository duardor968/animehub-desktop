import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Full navigation breadcrumb above a detail hero: clickable parent steps leading into the muted,
// non-clickable current page title ("Catálogo › Título", "Biblioteca › Franquicia › Título",
// "Horario › Título", "Descargas › Título").
RowLayout {
    id: root

    // [{text, go}] from the top place down to the page's parent.
    property var steps: []
    // Muted, non-clickable title of the current page shown at the end of the trail.
    property string current: ""

    readonly property bool hasCurrent: {
        if (current === "")
            return false
        if (steps.length > 0) {
            const last = steps[steps.length - 1]
            if (last && typeof last.go !== "function" && last.text === current)
                return false
        }
        return true
    }

    spacing: 6
    Accessible.role: Accessible.Grouping
    Accessible.name: "Ruta: " + steps.map(step => step.text)
                                     .concat(hasCurrent ? [current] : []).join(", ")

    Repeater {
        model: root.steps

        delegate: RowLayout {
            id: step

            required property int index
            required property var modelData

            readonly property bool clickable: typeof step.modelData.go === "function"
            readonly property real stepCap: root.hasCurrent
                                            ? Math.max(120, Math.round(root.width * 0.40))
                                            : root.width

            // A long franchise name gives way, cut at its end, before starving the current title.
            // Whole pixels: a layout rounds a fraction down and would cut a short name.
            Layout.fillWidth: !clickable && !root.hasCurrent
            Layout.preferredWidth: Math.min(Math.ceil(implicitWidth), stepCap)
            Layout.maximumWidth: Math.min(Math.ceil(implicitWidth), stepCap)
            Layout.minimumWidth: 0
            spacing: 6

            Glyph {
                visible: step.index > 0
                glyph: Theme.icon.chevronRight
                size: 9
                color: Theme.faint
                Layout.alignment: Qt.AlignVCenter
                Accessible.ignored: true
            }

            AbstractButton {
                id: link
                visible: step.clickable
                Layout.fillWidth: true
                Layout.preferredWidth: Math.ceil(implicitWidth)
                Layout.maximumWidth: Math.ceil(implicitWidth)
                Layout.minimumWidth: 0
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                implicitWidth: implicitContentWidth
                implicitHeight: implicitContentHeight
                text: step.modelData.text
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Link
                Accessible.onPressAction: clicked()
                onClicked: step.modelData.go()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                background: null
                contentItem: Label {
                    text: link.text
                    color: link.hovered || (link.activeFocus && InputModality.keyboard)
                           ? Theme.ink : Theme.muted
                    font.pixelSize: Theme.small
                    font.underline: link.hovered
                    elide: Text.ElideRight
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }

                FocusFrame {
                    visible: link.activeFocus && InputModality.keyboard
                    radiusInside: 2
                }
            }

            Label {
                visible: !step.clickable
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: step.modelData.text
                color: Theme.muted
                font.pixelSize: Theme.small
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
            }
        }
    }

    Glyph {
        visible: root.hasCurrent && root.steps.length > 0
        glyph: Theme.icon.chevronRight
        size: 9
        color: Theme.faint
        Layout.alignment: Qt.AlignVCenter
        Accessible.ignored: true
    }

    Label {
        visible: root.hasCurrent
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: root.current
        color: Theme.muted
        font.pixelSize: Theme.small
        elide: Text.ElideRight
        Accessible.role: Accessible.StaticText
    }

    Item {
        visible: !root.hasCurrent
        Layout.fillWidth: true
    }
}
