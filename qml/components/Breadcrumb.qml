import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Where the page sits, as Steam's store says it above a game: "Biblioteca › Kusuriya no
// Hitorigoto". Each step is a link back to that place, by mouse or keyboard; the trail ends at the
// page's parent, since the page's title right below names the page itself.
RowLayout {
    id: root

    // [{text, go}] from the top place down to the page's parent.
    property var steps: []

    spacing: 6
    Accessible.role: Accessible.Grouping
    Accessible.name: "Ruta: " + steps.map(step => step.text).join(", ")

    Repeater {
        model: root.steps

        delegate: RowLayout {
            id: step

            required property int index
            required property var modelData

            // A long franchise name gives way, cut at its end, before the page's edge.
            // Whole pixels: a layout rounds a fraction down and would cut a short name.
            Layout.fillWidth: true
            Layout.preferredWidth: Math.ceil(implicitWidth)
            Layout.maximumWidth: Math.ceil(implicitWidth)
            Layout.minimumWidth: 0
            spacing: 6

            Label {
                visible: step.index > 0
                text: "›"
                color: Theme.muted
                font.pixelSize: Theme.body
                Accessible.ignored: true
            }

            AbstractButton {
                id: link
                Layout.fillWidth: true
                Layout.preferredWidth: Math.ceil(implicitWidth)
                Layout.maximumWidth: Math.ceil(implicitWidth)
                Layout.minimumWidth: 0
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                text: step.modelData.text
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Link
                onClicked: step.modelData.go()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                background: null
                contentItem: Label {
                    text: link.text
                    color: link.hovered || link.visualFocus ? Theme.ink : Theme.muted
                    font.pixelSize: Theme.body
                    font.underline: link.hovered
                    elide: Text.ElideRight
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }

                FocusFrame {
                    visible: link.visualFocus
                    radiusInside: 2
                }
            }
        }
    }

    Item { Layout.fillWidth: true }
}
