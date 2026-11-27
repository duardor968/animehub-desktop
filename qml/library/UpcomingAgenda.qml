import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Today's airing episodes among followed library anime ("Hoy · <Día>"), with a direct
// "Ver horario ›" jump to the top-level Horario tab. Stays visible with a compact dignified
// empty block when 0 followed anime air today so the full schedule is always one click away.
Column {
    id: root

    // LibraryModel.todayHeading (e.g. "Hoy · Jueves") and LibraryModel.upcoming:
    // [{slug, title, posterUrl, episode, episodeText, time, timeText, state, stateLabel, badgeText, caption}]
    property string heading: "Hoy"
    property var items: []
    property bool rail: false
    property bool stale: false

    signal activated(string slug)
    signal openSchedule()

    readonly property var safeItems: items ?? []
    readonly property int itemCount: safeItems.length
    // Stacked mode: responsive 1..2 columns so time, episode and state chip breathe cleanly.
    readonly property int perRow: Math.max(1, Math.min(Math.max(1, itemCount),
                                                       Math.floor((width + Theme.posterGap) / (280 + Theme.posterGap))))
    readonly property real columnWidth: perRow === 1
                                        ? width
                                        : (width - (perRow - 1) * Theme.posterGap) / perRow

    spacing: 12

    RowLayout {
        width: root.width
        spacing: 8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: Theme.success
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            text: root.heading !== "" ? root.heading : "Hoy"
            color: Theme.ink
            font.pixelSize: root.rail ? 15 : Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }

        Label {
            visible: root.itemCount > 0
            Layout.alignment: Qt.AlignVCenter
            text: "(" + root.itemCount + ")"
            color: Theme.muted
            font.pixelSize: root.rail ? 14 : Theme.subtitle
        }

        Control {
            id: staleInfo
            visible: root.stale
            padding: 2
            focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.StaticText
            Accessible.name: "Horario en caché; los retrasos se ocultan hasta reconectar"
            ToolTip.visible: staleHover.hovered || (activeFocus && InputModality.keyboard)
            ToolTip.delay: 250
            ToolTip.text: Accessible.name

            HoverHandler { id: staleHover }

            contentItem: Glyph {
                glyph: Theme.icon.info
                size: 14
                color: staleHover.hovered ? Theme.ink : Theme.muted
            }

            FocusFrame {
                target: staleInfo
                radiusInside: 3
                visible: staleInfo.activeFocus && InputModality.keyboard
            }
        }

        // Stacked mode extends a hairline across the section, matching Shelf headers.
        Rectangle {
            visible: !root.rail
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.preferredHeight: 1
            color: Theme.stroke
        }

        // Rail mode pushes the schedule link cleanly to the right edge.
        Item {
            visible: root.rail
            Layout.fillWidth: true
        }

        AbstractButton {
            id: scheduleLink
            objectName: "openScheduleLink"
            Layout.alignment: Qt.AlignVCenter
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            leftPadding: 4
            rightPadding: 4
            topPadding: 3
            bottomPadding: 3
            text: "Ver horario ›"
            Accessible.name: "Ver horario completo"
            onClicked: root.openSchedule()
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()

            background: Rectangle {
                radius: 4
                color: scheduleLink.down ? Qt.rgba(47 / 255, 129 / 255, 247 / 255, 0.16)
                       : scheduleLink.hovered ? Qt.rgba(47 / 255, 129 / 255, 247 / 255, 0.08)
                       : "transparent"
                Behavior on color { ColorAnimation { duration: 100 } }
            }

            contentItem: Label {
                text: scheduleLink.text
                color: scheduleLink.hovered ? Theme.ink : Theme.accentText
                font.pixelSize: 12
                font.weight: Font.DemiBold
                Accessible.ignored: true
            }

            FocusFrame {
                radiusInside: 4
                visible: scheduleLink.activeFocus && InputModality.keyboard
            }
        }
    }

    Rectangle {
        visible: root.rail
        width: root.width
        height: 1
        color: Theme.stroke
        opacity: 0.7
    }

    // Dignified empty block when 0 followed anime air today.
    Label {
        visible: root.itemCount === 0
        width: root.width
        topPadding: root.rail ? 4 : 0
        bottomPadding: root.rail ? 4 : 0
        text: "Sin emisiones hoy"
        color: Theme.muted
        font.pixelSize: 13
    }

    // Clean AbstractButton row (replaces ItemDelegate to eliminate FluentWinUI3's stray left bar).
    component AgendaEntryRow: AbstractButton {
        id: entry

        required property var modelData

        readonly property string metaText: {
            const t = modelData.timeText ?? modelData.time ?? ""
            const ep = modelData.episodeText ?? (modelData.episode ? ("Ep. " + modelData.episode) : "")
            if (t !== "" && ep !== "")
                return t + " · " + ep
            if (t !== "")
                return t
            if (ep !== "")
                return ep
            return modelData.caption ?? ""
        }

        implicitHeight: Math.max(64, Math.ceil(rowLayout.implicitHeight) + topPadding + bottomPadding)
        leftPadding: 6
        rightPadding: 6
        topPadding: 6
        bottomPadding: 6
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        text: modelData.title ?? ""
        onClicked: root.activated(modelData.slug)
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Accessible.name: (modelData.title ?? "") + ", " + root.heading + ", "
                         + (modelData.caption ?? metaText)
        ToolTip.visible: hovered && name.truncated
        ToolTip.delay: 500
        ToolTip.text: modelData.title ?? ""

        background: Rectangle {
            radius: 6
            color: entry.down ? Qt.rgba(1, 1, 1, 0.08)
                   : entry.hovered ? Qt.rgba(1, 1, 1, 0.045) : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
        }

        contentItem: RowLayout {
            id: rowLayout
            spacing: 10

            Poster {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 51
                Layout.alignment: Qt.AlignVCenter
                source: entry.modelData.posterUrl ?? ""
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: 4

                Label {
                    id: name
                    Layout.fillWidth: true
                    text: entry.modelData.title ?? ""
                    color: entry.hovered ? "#ffffff" : Theme.ink
                    font.pixelSize: root.rail ? 13 : Theme.body
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: entry.metaText
                        color: Theme.muted
                        font.pixelSize: Theme.caption
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }

                    ScheduleStateChip {
                        Layout.alignment: Qt.AlignVCenter
                        status: entry.modelData.state ?? ""
                        label: entry.modelData.stateLabel ?? entry.modelData.badgeText ?? ""
                        compact: true
                    }
                }
            }
        }

        FocusFrame {
            radiusInside: 6
            visible: entry.activeFocus && InputModality.keyboard
        }
    }

    // Rail mode: single-column vertical list.
    Column {
        visible: root.rail && root.itemCount > 0
        width: root.width
        spacing: 4

        Repeater {
            model: root.rail ? root.safeItems : []

            delegate: AgendaEntryRow {
                width: root.width
            }
        }
    }

    // Stacked mode: responsive 1..2 column flow when the window is narrow.
    Flow {
        visible: !root.rail && root.itemCount > 0
        width: root.width
        spacing: Theme.posterGap

        Repeater {
            model: !root.rail ? root.safeItems : []

            delegate: AgendaEntryRow {
                width: root.columnWidth
            }
        }
    }
}
