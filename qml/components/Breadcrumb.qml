import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Where the page sits, as Steam's store says it above a game: "Biblioteca › Kusuriya no
// Hitorigoto › 3rd Season". Each step before the page is a link back to that place, by mouse or
// keyboard; the page's own name closes the line.
RowLayout {
    id: root

    // [{text, go}] from the top place down to the page, which is `current`.
    property var steps: []
    property string current

    spacing: 6
    Accessible.role: Accessible.Grouping
    Accessible.name: "Ruta: " + steps.map(step => step.text).concat([current]).join(", ")

    Repeater {
        model: root.steps

        delegate: RowLayout {
            id: step

            required property var modelData

            Layout.fillWidth: false
            spacing: 6

            AbstractButton {
                id: link
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
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }

                FocusFrame {
                    visible: link.visualFocus
                    radiusInside: 2
                }
            }

            Label {
                text: "›"
                color: Theme.muted
                font.pixelSize: Theme.body
                Accessible.ignored: true
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: root.current
        color: Theme.muted
        font.pixelSize: Theme.body
        elide: Text.ElideRight
        Accessible.ignored: true
    }
}
