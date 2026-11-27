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
    radius: 6
    color: "#141922"
    border.width: 1
    border.color: "#252c38"

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
                onClicked: root.scopeSelected(modelData.id)
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onSpacePressed: clicked()

                background: Rectangle {
                    radius: 4
                    color: segBtn.active ? "#232b38"
                           : segBtn.hovered ? Qt.rgba(1, 1, 1, 0.045) : "transparent"
                    border.width: segBtn.active ? 1 : 0
                    border.color: "#333f52"
                    Behavior on color { ColorAnimation { duration: 110 } }
                }

                contentItem: Label {
                    id: segLbl
                    text: segBtn.modelData.label
                    color: segBtn.active ? "#f2f5f9"
                           : segBtn.hovered ? Theme.ink : Theme.muted
                    font.pixelSize: 12
                    font.weight: segBtn.active ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Accessible.ignored: true
                }

                FocusFrame {
                    radiusInside: 4
                    visible: segBtn.activeFocus && InputModality.keyboard
                }
            }
        }
    }
}
