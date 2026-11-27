import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Primary brand accent button with crisp #ffffff text and icon over #2f81f7 (fixing Qt 6.11
// FluentWinUI3 Dark's black-on-accent default built for pastel #60cdff), matching SplitAction.
AbstractButton {
    id: control

    property string glyphCode: ""
    property int iconSize: 13
    property int fontPixelSize: Theme.body
    property bool emphasized: true

    implicitWidth: Math.max(88, row.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 34
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    background: Rectangle {
        radius: 5
        color: {
            if (!control.enabled)
                return Theme.accentDisabled
            if (!control.emphasized)
                return control.down ? "#252b34" : (control.hovered ? "#2c3340" : Theme.raised)
            return control.down ? "#246bd1" : (control.hovered ? "#3d8bfd" : Theme.accent)
        }
        border.width: control.emphasized ? 0 : 1
        border.color: control.hovered && control.enabled ? "#3b4351" : Theme.stroke

        Behavior on color { ColorAnimation { duration: 110 } }
    }

    contentItem: RowLayout {
        id: row
        spacing: control.glyphCode !== "" && control.text !== "" ? 8 : 0

        Item { Layout.fillWidth: true }

        Image {
            visible: control.glyphCode !== ""
            Layout.preferredWidth: control.iconSize
            Layout.preferredHeight: control.iconSize
            Layout.alignment: Qt.AlignVCenter
            source: control.glyphCode !== ""
                    ? Theme.source(control.glyphCode + "?color=" + (control.enabled
                        ? (control.emphasized ? "ffffff" : "e9ebee") : "9aa1ab"))
                    : ""
            sourceSize.width: control.iconSize * 2
            sourceSize.height: control.iconSize * 2
            smooth: true
            Accessible.ignored: true
        }

        Label {
            visible: control.text !== ""
            text: control.text
            color: !control.enabled ? Theme.muted
                   : (control.emphasized ? Theme.accentInk : Theme.ink)
            font.pixelSize: control.fontPixelSize
            font.weight: control.emphasized ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true }
    }

    FocusFrame {
        visible: control.activeFocus && InputModality.keyboard
        radiusInside: 5
    }
}
