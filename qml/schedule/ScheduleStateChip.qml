import QtQuick
import QtQuick.Controls

// Strictly 4 schedule states: Emitido, Final, Retrasado, Próximo (never "Ahora").
// "Final" is 100% visually identical to "Emitido" (#163626 fill, #6ee7b7 text); only the label differs.
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

    readonly property color fg: isGreen ? "#6ee7b7"
                              : isLate ? "#fbbf24"
                              : "#7cc0ff"
    readonly property color bg: isGreen ? "#163626"
                              : isLate ? "#3b2c13"
                              : "#172b47"

    visible: labelText !== ""
    implicitWidth: chipLbl.implicitWidth + (compact ? 12 : 16)
    implicitHeight: compact ? 20 : 22
    height: implicitHeight
    radius: 4
    color: bg
    border.width: 1
    border.color: Qt.rgba(fg.r, fg.g, fg.b, 0.28)
    Accessible.role: Accessible.StaticText
    Accessible.name: labelText

    Label {
        id: chipLbl
        anchors.centerIn: parent
        text: chip.labelText
        color: chip.fg
        font.pixelSize: chip.compact ? 10 : 11
        font.weight: Font.DemiBold
        Accessible.ignored: true
    }
}
