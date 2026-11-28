import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One setting as in Windows' and Steam's settings: its name on the left, with an ⓘ when the
// control alone cannot show a consequence; its value and its controls against the right edge,
// the same edge on every row; a hairline under it. In a narrow window they go under the name.
Item {
    id: row

    property string label
    property string info
    // The current value as text, before the controls that change it.
    property string value
    // A long value (a folder) under the name instead, muted, with the row's width.
    property string detail
    property color valueColor: Theme.ink
    // Why its controls cannot be used now, told over them on hover; empty while they can.
    property string unavailable
    default property alias content: slot.data

    // What a name keeps beside a long value.
    readonly property int nameWidth: 180
    readonly property bool stacked: width < 520
    readonly property string shownDetail: detail !== "" ? detail : unavailable
    // Rows taller than one control line keep the name level with their first line.
    readonly property bool tall: end.implicitHeight > 40 && shownDetail === ""
    readonly property real nameLine: title.height / Math.max(1, title.lineCount)

    Layout.fillWidth: true
    implicitHeight: stacked ? name.height + end.implicitHeight + 30
                            : Math.max(52, end.implicitHeight + 20, name.height + 20)
    Accessible.description: unavailable !== "" ? unavailable : info

    Item {
        id: name

        width: row.stacked ? row.width : row.width - end.width - 24
        height: title.height + (detailLabel.visible ? detailLabel.height + 2 : 0)
        y: row.stacked ? 12 : row.tall ? 10 + (32 - row.nameLine) / 2 : (row.height - height) / 2

        Label {
            id: title
            width: Math.min(implicitWidth, name.width - (mark.visible ? mark.width + 4 : 0))
            text: row.label
            color: row.unavailable === "" ? Theme.ink : Theme.muted
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        AbstractButton {
            id: mark
            property bool toggledOpen: false

            x: title.contentWidth + 4
            y: (row.nameLine - height) / 2
            width: 22
            height: 22
            padding: 0
            visible: row.info !== ""
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            activeFocusOnTab: visible
            onActiveFocusChanged: if (!activeFocus) toggledOpen = false
            onClicked: toggledOpen = !toggledOpen
            Keys.onReturnPressed: event => {
                event.accepted = true
                mark.click()
            }
            Keys.onEnterPressed: event => {
                event.accepted = true
                mark.click()
            }
            Keys.onEscapePressed: event => {
                if (mark.toggledOpen) {
                    mark.toggledOpen = false
                    event.accepted = true
                }
            }
            Accessible.role: Accessible.Button
            Accessible.name: "Más información sobre " + row.label
            Accessible.description: row.info
            Accessible.onPressAction: mark.click()
            ToolTip.visible: row.info !== ""
                             && (hovered || toggledOpen || (activeFocus && InputModality.keyboard))
            ToolTip.delay: toggledOpen || (activeFocus && InputModality.keyboard)
                           ? 0 : Theme.tooltipDelay
            ToolTip.timeout: toggledOpen ? 0 : Theme.tooltipTimeout
            ToolTip.text: row.info

            background: Rectangle {
                radius: Theme.radiusSm
                color: mark.down ? Theme.pressSubtle
                       : mark.hovered ? Theme.hoverSubtle : "transparent"

                FocusFrame {
                    visible: mark.activeFocus && InputModality.keyboard
                    radiusInside: Theme.radiusSm
                }
            }

            contentItem: Item {
                Glyph {
                    anchors.centerIn: parent
                    glyph: Theme.icon.info
                    size: 14
                    color: mark.hovered || mark.activeFocus ? Theme.ink : Theme.muted
                }
            }
        }
        Label {
            id: detailLabel
            y: title.height + 2
            width: name.width
            visible: row.shownDetail !== ""
            text: row.shownDetail
            color: row.detail !== "" ? Theme.muted : Theme.faint
            font.pixelSize: Theme.caption
            elide: Text.ElideMiddle
            ToolTip.visible: detailHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
            HoverHandler { id: detailHover }
        }
    }

    RowLayout {
        id: end

        // A long value gives way down to the room its name keeps.
        readonly property real room: row.width - 24
                                     - Math.min(row.nameWidth, title.implicitWidth
                                                + (mark.visible ? mark.width + 4 : 0))

        width: row.stacked ? row.width : Math.min(implicitWidth, room)
        x: row.width - width
        y: row.stacked ? name.y + name.height + 6 : row.tall ? 10 : (row.height - height) / 2
        spacing: 0

        Label {
            id: shown
            Layout.fillWidth: true
            // Whole pixels: the layout rounds, and a fraction short would elide it.
            Layout.preferredWidth: Math.ceil(implicitWidth)
            Layout.maximumWidth: Math.ceil(implicitWidth)
            // A value alone ends at the edge too.
            Layout.rightMargin: slot.implicitWidth > 0 ? 12 : 0
            visible: row.value !== ""
            text: row.value
            color: row.valueColor
            elide: Text.ElideMiddle
            ToolTip.visible: valueHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.timeout: Theme.tooltipTimeout
            ToolTip.text: text
            HoverHandler { id: valueHover }
        }
        RowLayout {
            id: slot
            Layout.fillWidth: true
            Layout.preferredWidth: Math.ceil(implicitWidth)
            Layout.maximumWidth: Math.ceil(implicitWidth)
            enabled: row.unavailable === ""
            spacing: 8
        }
        // Under the name, the value and the controls start at the left.
        Item { Layout.fillWidth: true }
    }

    // A disabled control gets no hover or focus of its own: expose the reason to hover,
    // keyboard navigation, and screen readers.
    Item {
        id: reasonTarget
        x: end.x + slot.x
        y: end.y + slot.y
        width: slot.width
        height: slot.height
        visible: row.unavailable !== ""
        activeFocusOnTab: visible
        Accessible.role: Accessible.StaticText
        Accessible.name: row.label + ", no disponible"
        Accessible.description: row.unavailable
        HoverHandler { id: reasonHover }
        ToolTip.visible: row.unavailable !== ""
                         && (reasonHover.hovered || (activeFocus && InputModality.keyboard))
        ToolTip.delay: activeFocus && InputModality.keyboard ? 0 : Theme.tooltipDelay
        ToolTip.timeout: Theme.tooltipTimeout
        ToolTip.text: row.unavailable

        FocusFrame {
            visible: reasonTarget.activeFocus && InputModality.keyboard
            radiusInside: Theme.radius
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }
}
