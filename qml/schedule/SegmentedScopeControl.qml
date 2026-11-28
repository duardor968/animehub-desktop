import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Single unified Fluent segmented switcher (Por día | Semana) — calm dark pill, not loud accent fill.
Rectangle {
    id: root

    property string currentScope: "day" // "day" | "week"
    property bool fillWidth: false

    signal scopeSelected(string scope)

    implicitHeight: 32
    implicitWidth: fillWidth ? 208 : segRow.implicitWidth + 6
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.stroke
    Accessible.role: Accessible.Grouping
    Accessible.name: "Vista del horario"

    RowLayout {
        id: segRow
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2

        Repeater {
            model: [
                { id: "day", label: "Por día" },
                { id: "week", label: "Semana" }
            ]

            delegate: AbstractButton {
                id: segBtn

                required property var modelData
                readonly property bool active: root.currentScope === modelData.id

                Layout.fillWidth: root.fillWidth
                Layout.fillHeight: true
                implicitWidth: root.fillWidth ? 0 : segLbl.implicitWidth + 24
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                text: modelData.label
                Accessible.role: Accessible.RadioButton
                Accessible.checkable: true
                Accessible.checked: active
                Accessible.name: modelData.label
                Accessible.onPressAction: segBtn.clicked()
                onClicked: root.scopeSelected(modelData.id)
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    segBtn.clicked()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    segBtn.clicked()
                }
                Keys.onSpacePressed: event => {
                    event.accepted = true
                    segBtn.clicked()
                }

                background: Rectangle {
                    radius: Theme.radiusSm
                    color: segBtn.active ? Theme.hover
                           : segBtn.down ? Theme.pressSubtle
                           : segBtn.hovered ? Theme.hoverSubtle : "transparent"
                    border.width: segBtn.active ? 1 : 0
                    border.color: Theme.stroke
                    Behavior on color { ColorAnimation { duration: 110 } }
                }

                contentItem: Label {
                    id: segLbl
                    text: segBtn.modelData.label
                    color: segBtn.active ? Theme.ink
                           : segBtn.hovered ? Theme.ink : Theme.muted
                    font.pixelSize: Theme.small
                    font.weight: segBtn.active ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                FocusFrame {
                    radiusInside: Theme.radiusSm
                    visible: segBtn.activeFocus && InputModality.keyboard
                }
            }
        }
    }
}
