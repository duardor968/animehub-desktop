import QtQuick
import QtQuick.Controls
import AnimeHub

// Strictly 4 harmonized schedule states: Emitido, Final, Retrasado, Próximo (never "Ahora").
// Aligned with Theme.success / Theme.warning / Theme.accentText; "Final" shares Theme.success with "Emitido".
Rectangle {
    id: chip

    property string status: ""
    property string label: ""
    property bool compact: false

    readonly property string normalized: status.toLowerCase()
    readonly property bool isAired: normalized === "aired" || normalized === "emitido"
    readonly property bool isFinale: normalized === "finale" || normalized === "final"
    readonly property bool isLate: normalized === "late" || normalized === "retrasado"
    readonly property bool isUpcoming: normalized === "upcoming" || normalized === "proximo"
                                       || normalized === "próximo"
    readonly property bool isGreen: isAired || isFinale

    readonly property string labelText: {
        if (label !== "")
            return label
        if (isAired)
            return "Emitido"
        if (isFinale)
            return "Final"
        if (isLate)
            return "Retrasado"
        if (isUpcoming)
            return "Próximo"
        return ""
    }

    readonly property color fg: isGreen ? Theme.success
                              : isLate ? Theme.warning
                              : Theme.accentText
    readonly property color bg: isGreen ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.16)
                              : isLate ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                              : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
    readonly property color bd: isGreen ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.32)
                              : isLate ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.32)
                              : Qt.rgba(Theme.accentText.r, Theme.accentText.g, Theme.accentText.b, 0.32)

    visible: labelText !== ""
    implicitWidth: chipLbl.implicitWidth + (compact ? 12 : 16)
    implicitHeight: compact ? 20 : 22
    height: implicitHeight
    radius: Theme.radiusSm
    color: bg
    border.width: 1
    border.color: bd
    Accessible.role: Accessible.StaticText
    Accessible.name: labelText

    Label {
        id: chipLbl
        anchors.centerIn: parent
        text: chip.labelText
        color: chip.fg
        font.pixelSize: chip.compact ? 10 : Theme.micro
        font.weight: Font.DemiBold
        Accessible.ignored: true
    }
}
