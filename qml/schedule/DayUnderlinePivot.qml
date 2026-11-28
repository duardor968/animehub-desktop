import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Flush horizontal underline day pivot in strict chronological Mon..Sun order (never reordered).
Item {
    id: root

    required property var daysModel
    required property var countsByDay
    property int activeWeekday: 0
    property bool compactLabels: width < 880

    signal daySelected(int weekday)

    implicitHeight: 38

    // Full-width subtle base hairline.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
        opacity: 0.85
    }

    RowLayout {
        anchors.fill: parent
        spacing: root.compactLabels ? 4 : 10

        Repeater {
            model: root.daysModel

            delegate: AbstractButton {
                id: tabBtn

                required property var modelData
                readonly property bool active: root.activeWeekday === modelData.weekday
                readonly property int cnt: (root.countsByDay && modelData.weekday < root.countsByDay.length)
                                           ? root.countsByDay[modelData.weekday] : 0

                Layout.fillHeight: true
                implicitWidth: tabRow.implicitWidth + (root.compactLabels ? 16 : 22)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                text: modelData.fullName
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.fullName + (modelData.isToday ? " (Hoy)" : "")
                                 + ", " + cnt + (cnt === 1 ? " emisión" : " emisiones")
                Accessible.onPressAction: tabBtn.clicked()
                ToolTip.visible: root.compactLabels && (hovered || (activeFocus && InputModality.keyboard))
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.timeout: Theme.tooltipTimeout
                ToolTip.text: modelData.fullName + (modelData.isToday ? " (Hoy)" : "")
                onClicked: root.daySelected(modelData.weekday)
                Keys.onReturnPressed: event => {
                    event.accepted = true
                    tabBtn.clicked()
                }
                Keys.onEnterPressed: event => {
                    event.accepted = true
                    tabBtn.clicked()
                }
                Keys.onSpacePressed: event => {
                    event.accepted = true
                    tabBtn.clicked()
                }

                background: Item {
                    // Subtle hover tint only — no rectangular box borders.
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 2
                        anchors.bottomMargin: 3
                        radius: Theme.radiusSm
                        color: !tabBtn.active && tabBtn.down ? Theme.pressSubtle
                               : !tabBtn.active && tabBtn.hovered ? Theme.hoverSubtle : "transparent"
                    }

                    // Crisp 2 px #2f81f7 bottom underline resting directly on the hairline.
                    Rectangle {
                        visible: tabBtn.active
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        topLeftRadius: 2
                        topRightRadius: 2
                        color: Theme.accent
                    }
                }

                contentItem: Row {
                    id: tabRow
                    anchors.centerIn: parent
                    spacing: 6

                    // Clean 6 px emerald dot on the left of Today inside fixed Mon..Sun order.
                    Rectangle {
                        visible: tabBtn.modelData.isToday
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6
                        height: 6
                        radius: 3
                        color: Theme.success
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.compactLabels ? tabBtn.modelData.shortName : tabBtn.modelData.fullName
                        color: tabBtn.active ? Theme.ink
                               : tabBtn.hovered ? Theme.ink : Theme.muted
                        font.pixelSize: Theme.small
                        font.weight: tabBtn.active || tabBtn.modelData.isToday ? Font.DemiBold : Font.Normal
                        Accessible.ignored: true
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(tabBtn.cnt)
                        color: tabBtn.active ? Theme.accentText : Theme.faint
                        font.pixelSize: Theme.caption
                        font.weight: tabBtn.active ? Font.DemiBold : Font.Normal
                        Accessible.ignored: true
                    }
                }

                FocusFrame {
                    radiusInside: Theme.radiusSm
                    visible: tabBtn.activeFocus && InputModality.keyboard
                }
            }
        }

        Item { Layout.fillWidth: true }
    }
}
