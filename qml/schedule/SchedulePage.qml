import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Top-level Horario tab (Ronda 4 Variant A + Duardo's adjustments):
// - Strict Monday..Sunday (0..6) order in BOTH Por día and Semana (never reordering Today first)
// - Header controls (Por día | Semana + Solo los que sigo) grouped left right after the count
// - Live local time, persisted view mode and following filter, clean stale/empty states
Item {
    id: root

    required property AppController app
    property bool active: false

    signal openAnime(string slug)

    readonly property LibraryModel library: app.library
    readonly property string scope: app.settings.scheduleViewMode === "week" ? "week" : "day"
    readonly property bool followingOnly: app.settings.scheduleFollowingOnly

    property int activeWeekday: library ? library.todayWeekday : 0
    property bool userPickedDay: false

    // Fixed chronological Monday..Sunday metadata; Today is highlighted in place, never moved to 0.
    readonly property var weekDays: {
        const today = library ? library.todayWeekday : 0
        const names = [
            { full: "Lunes", short: "Lun" },
            { full: "Martes", short: "Mar" },
            { full: "Miércoles", short: "Mié" },
            { full: "Jueves", short: "Jue" },
            { full: "Viernes", short: "Vie" },
            { full: "Sábado", short: "Sáb" },
            { full: "Domingo", short: "Dom" }
        ]
        return names.map((d, i) => ({
            weekday: i,
            fullName: d.full,
            shortName: d.short,
            isToday: i === today,
            todayLabel: i === today ? ("Hoy · " + d.full) : d.full
        }))
    }

    // Reactive day buckets (0..6) filtered by followingOnly.
    readonly property var groupedByDay: {
        const buckets = [[], [], [], [], [], [], []]
        const all = library ? (library.weeklySchedule ?? []) : []
        const only = root.followingOnly
        for (let i = 0; i < all.length; ++i) {
            const item = all[i]
            if (only && !item.inLibrary)
                continue
            const wd = item.weekday
            if (wd >= 0 && wd <= 6)
                buckets[wd].push(item)
        }
        return buckets
    }

    readonly property var countsByDay: groupedByDay.map(b => b.length)
    readonly property int totalFilteredCount: {
        let n = 0
        for (let i = 0; i < countsByDay.length; ++i)
            n += countsByDay[i]
        return n
    }
    readonly property int totalUnfilteredCount: library ? (library.weeklySchedule ?? []).length : 0
    readonly property var activeDayItems: (activeWeekday >= 0 && activeWeekday <= 6)
                                          ? groupedByDay[activeWeekday] : []
    readonly property int activeDayUnfilteredCount: {
        const all = library ? (library.weeklySchedule ?? []) : []
        let n = 0
        for (let i = 0; i < all.length; ++i) {
            if (all[i].weekday === activeWeekday)
                ++n
        }
        return n
    }
    readonly property bool loadingInitial: library ? (!library.scheduleLoaded && totalUnfilteredCount === 0)
                                                   : false

    Component.onCompleted: {
        if (library)
            activeWeekday = library.todayWeekday
        if (active && library)
            library.refreshSchedule()
    }

    onActiveChanged: {
        if (active && library)
            library.refreshSchedule()
    }

    Connections {
        target: root.library
        function onScheduleChanged() {
            if (!root.userPickedDay && root.library)
                root.activeWeekday = root.library.todayWeekday
        }
    }

    function selectDay(weekday) {
        userPickedDay = true
        activeWeekday = weekday
    }

    function scrollToDay(weekday) {
        const it = weekAgendaRepeater.itemAt(weekday)
        if (!it || !it.visible)
            return
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        const targetY = col.y + weekAgendaCol.y + it.y - 12
        flick.contentY = Math.min(maxY, Math.max(0, targetY))
    }

    function ensureVisible(item) {
        if (!item || !root.active || !InputModality.keyboard)
            return
        let inside = false
        for (let p = item; p && !inside; p = p.parent)
            inside = p === col
        if (!inside)
            return
        const top = item.mapToItem(col, 0, 0).y + col.y
        if (top - 16 < flick.contentY)
            flick.contentY = Math.max(0, top - 16)
        else if (top + item.height + 16 > flick.contentY + flick.height)
            flick.contentY = Math.min(top + item.height + 16 - flick.height,
                                      Math.max(0, flick.contentHeight - flick.height))
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            root.ensureVisible(root.Window.activeFocusItem)
        }
    }

    // Flush day-jump strip for Semana mode in strict Mon..Sun order.
    component WeekJumpStrip: Item {
        id: jumpStrip

        property bool compact: width < 860
        signal jumpRequested(int weekday)

        implicitHeight: 34

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.stroke
            opacity: 0.75
        }

        RowLayout {
            anchors.fill: parent
            spacing: jumpStrip.compact ? 4 : 8

            Label {
                visible: !jumpStrip.compact
                text: "Ir a:"
                color: Theme.faint
                font.pixelSize: 12
                rightPadding: 2
            }

            Repeater {
                model: root.weekDays

                delegate: AbstractButton {
                    id: jBtn

                    required property var modelData
                    readonly property int cnt: root.countsByDay[modelData.weekday] ?? 0
                    readonly property bool active: root.activeWeekday === modelData.weekday

                    Layout.fillHeight: true
                    implicitWidth: jRow.implicitWidth + (jumpStrip.compact ? 12 : 16)
                    enabled: cnt > 0
                    opacity: cnt > 0 ? 1.0 : 0.38
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    text: modelData.todayLabel
                    Accessible.name: "Ir a " + modelData.fullName + (modelData.isToday ? " (Hoy)" : "")
                                     + ", " + cnt + (cnt === 1 ? " emisión" : " emisiones")
                    onClicked: {
                        root.selectDay(modelData.weekday)
                        jumpStrip.jumpRequested(modelData.weekday)
                    }
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()

                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            anchors.topMargin: 2
                            anchors.bottomMargin: 3
                            radius: 4
                            color: jBtn.hovered && !jBtn.active ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                        }

                        Rectangle {
                            visible: jBtn.active
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
                        id: jRow
                        anchors.centerIn: parent
                        spacing: 5

                        Rectangle {
                            visible: jBtn.modelData.isToday
                            anchors.verticalCenter: parent.verticalCenter
                            width: 5
                            height: 5
                            radius: 3
                            color: Theme.success
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: jumpStrip.compact
                                  ? (jBtn.modelData.isToday ? "Hoy" : jBtn.modelData.shortName)
                                  : jBtn.modelData.todayLabel
                            color: jBtn.active ? "#f2f5f9" : jBtn.hovered ? Theme.ink : Theme.muted
                            font.pixelSize: 12
                            font.weight: jBtn.active || jBtn.modelData.isToday ? Font.DemiBold : Font.Normal
                            Accessible.ignored: true
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(jBtn.cnt)
                            color: jBtn.active ? Theme.accentText : Theme.faint
                            font.pixelSize: 11
                            Accessible.ignored: true
                        }
                    }

                    FocusFrame {
                        radiusInside: 4
                        visible: jBtn.activeFocus && InputModality.keyboard
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + col.y + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: col

            readonly property int padX: flick.width < 900 ? 22 : 30
            readonly property bool compactV: flick.height < 500
            readonly property bool wrapHeader: headerTitleGroup.implicitWidth
                                               + headerControlsGroup.implicitWidth + 24 > width

            x: padX
            y: compactV ? 12 : 20
            width: Math.min(Math.max(0, flick.width - 2 * padX), 1220)
            spacing: compactV ? 12 : 16

            // Header row: Horario -> Hora local · N en emisión -> 22 px -> Por día | Semana -> 16 px -> Solo los que sigo -> fillWidth
            GridLayout {
                Layout.fillWidth: true
                columns: col.wrapHeader ? 1 : 3
                columnSpacing: 22
                rowSpacing: 10

                RowLayout {
                    id: headerTitleGroup
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 14

                    Label {
                        text: "Horario"
                        color: "#f2f5f9"
                        font.pixelSize: col.width < 860 ? 21 : 24
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
                    }

                    Row {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 6

                        Glyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icon.clock
                            size: 12
                            color: Theme.faint
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Hora local: " + (root.library ? root.library.localTimeText : "")
                            color: Theme.muted
                            font.pixelSize: 13
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "·"
                            color: Theme.faint
                            font.pixelSize: 13
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.followingOnly
                                  ? (root.totalFilteredCount
                                     + (root.totalFilteredCount === 1 ? " seguido" : " seguidos"))
                                  : (root.totalFilteredCount + " en emisión")
                            color: Theme.faint
                            font.pixelSize: 13
                        }
                    }
                }

                RowLayout {
                    id: headerControlsGroup
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 16

                    SegmentedScopeControl {
                        currentScope: root.scope
                        onScopeSelected: s => root.app.settings.scheduleViewMode = s
                    }

                    RowLayout {
                        spacing: 6

                        Label {
                            text: "Solo los que sigo"
                            color: Theme.ink
                            font.pixelSize: 13
                        }

                        Switch {
                            checked: root.followingOnly
                            Accessible.name: "Solo los que sigo"
                            onToggled: root.app.settings.scheduleFollowingOnly = checked
                        }
                    }
                }

                Item {
                    visible: !col.wrapHeader
                    Layout.fillWidth: true
                }
            }

            // Stale warning strip — shown ONLY when scheduleStale is true.
            Rectangle {
                visible: root.library ? root.library.scheduleStale : false
                Layout.fillWidth: true
                implicitHeight: staleRow.implicitHeight + 16
                radius: Theme.radius
                color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10)
                border.width: 1
                border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.30)

                RowLayout {
                    id: staleRow
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 10

                    Glyph {
                        glyph: Theme.icon.warning
                        size: 14
                        color: Theme.warning
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "No se ha podido actualizar el horario; se muestra la última información disponible."
                        color: Theme.ink
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                }
            }

            // Por día: flush underline day pivot (fixed Mon..Sun order).
            DayUnderlinePivot {
                visible: root.scope === "day"
                Layout.fillWidth: true
                daysModel: root.weekDays
                countsByDay: root.countsByDay
                activeWeekday: root.activeWeekday
                onDaySelected: wd => root.selectDay(wd)
            }

            // Initial loading indicator before first schedule response arrives.
            RowLayout {
                visible: root.loadingInitial
                Layout.fillWidth: true
                Layout.topMargin: 24
                spacing: 10

                BusyIndicator {
                    running: parent.visible
                    implicitWidth: 20
                    implicitHeight: 20
                }

                Label {
                    text: "Cargando el horario…"
                    color: Theme.muted
                    font.pixelSize: 14
                }
            }

            // Por día: cohesive elevated poster grid (left-aligned for any count 1..N).
            Grid {
                id: dayGrid
                visible: root.scope === "day" && !root.loadingInitial && root.activeDayItems.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 2

                readonly property int gap: 16
                readonly property int maxCols: 5
                readonly property int minCardWidth: 168
                readonly property int maxCardWidth: 236
                readonly property int cols: Math.max(2, Math.min(maxCols,
                                                                 Math.floor((Math.max(0, col.width) + gap) / (minCardWidth + gap))))
                readonly property int cardWidth: Math.max(120, Math.min(maxCardWidth,
                                                                        Math.floor((Math.max(0, col.width) - (cols - 1) * gap) / cols)))

                columns: cols
                columnSpacing: gap
                rowSpacing: 18

                Repeater {
                    model: root.scope === "day" ? root.activeDayItems : []

                    delegate: SchedulePosterCard {
                        required property var modelData
                        width: dayGrid.cardWidth
                        title: modelData.title ?? ""
                        epNumber: modelData.episode ?? ""
                        timeText: modelData.time ?? ""
                        status: modelData.state ?? ""
                        stateLabel: modelData.stateLabel ?? ""
                        followed: Boolean(modelData.inLibrary)
                        poster: modelData.posterUrl ?? ""
                        onActivated: root.openAnime(modelData.slug)
                    }
                }
            }

            // Por día empty state.
            ColumnLayout {
                visible: root.scope === "day" && !root.loadingInitial && root.activeDayItems.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 20
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    text: (root.followingOnly && root.activeDayUnfilteredCount > 0)
                          ? "No sigues ningún anime en emisión este día"
                          : "Sin episodios este día"
                    color: Theme.muted
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                Button {
                    visible: root.followingOnly && root.activeDayUnfilteredCount > 0
                    text: "Mostrar todos"
                    onClicked: root.app.settings.scheduleFollowingOnly = false
                }
            }

            // Semana: flush day-jump strip (fixed Mon..Sun order).
            WeekJumpStrip {
                visible: root.scope === "week" && !root.loadingInitial && root.totalFilteredCount > 0
                Layout.fillWidth: true
                onJumpRequested: wd => root.scrollToDay(wd)
            }

            // Semana: clean continuous vertical agenda in strict Mon..Sun order (zero nested boxes).
            ColumnLayout {
                id: weekAgendaCol
                visible: root.scope === "week" && !root.loadingInitial && root.totalFilteredCount > 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 22

                Repeater {
                    id: weekAgendaRepeater
                    model: root.scope === "week" ? root.weekDays : []

                    delegate: ColumnLayout {
                        id: daySec

                        required property var modelData
                        readonly property var dayItems: root.groupedByDay[modelData.weekday] ?? []

                        visible: dayItems.length > 0
                        Layout.fillWidth: true
                        spacing: 0

                        // Day section header with 3 px #2f81f7 left accent bar + emerald dot on Today.
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 8
                            spacing: 9

                            Rectangle {
                                Layout.preferredWidth: 3
                                Layout.preferredHeight: 17
                                Layout.alignment: Qt.AlignVCenter
                                radius: 2
                                color: daySec.modelData.isToday ? Theme.accent : "#343d4d"
                            }

                            Rectangle {
                                visible: daySec.modelData.isToday
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                Layout.alignment: Qt.AlignVCenter
                                radius: 3
                                color: Theme.success
                            }

                            Label {
                                Layout.alignment: Qt.AlignVCenter
                                text: daySec.modelData.todayLabel
                                color: daySec.modelData.isToday ? "#f3f6fa" : Theme.ink
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                Accessible.role: Accessible.Heading
                            }

                            Label {
                                Layout.alignment: Qt.AlignVCenter
                                text: daySec.dayItems.length
                                      + (daySec.dayItems.length === 1 ? " emisión" : " emisiones")
                                color: Theme.faint
                                font.pixelSize: 12
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 6
                                Layout.preferredHeight: 1
                                Layout.alignment: Qt.AlignVCenter
                                color: Theme.stroke
                                opacity: 0.6
                            }
                        }

                        // Crisp scannable horizontal rows separated by subtle 1 px hairlines.
                        Repeater {
                            model: daySec.dayItems

                            delegate: AbstractButton {
                                id: rowBtn

                                required property var modelData
                                required property int index

                                Layout.fillWidth: true
                                implicitHeight: 68
                                padding: 0
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                text: modelData.title ?? ""
                                Accessible.name: (modelData.title ?? "")
                                                 + (modelData.episode ? ", episodio " + modelData.episode : "")
                                                 + (modelData.time ? ", " + modelData.time : "")
                                                 + (modelData.stateLabel ? ", " + modelData.stateLabel : "")
                                                 + (modelData.inLibrary ? ", en tu biblioteca" : "")
                                onClicked: root.openAnime(modelData.slug)
                                Keys.onReturnPressed: clicked()
                                Keys.onEnterPressed: clicked()
                                ToolTip.visible: hovered && rowTitle.truncated
                                ToolTip.delay: 500
                                ToolTip.text: modelData.title ?? ""

                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.leftMargin: -10
                                        anchors.rightMargin: -10
                                        radius: 6
                                        color: rowBtn.down ? Qt.rgba(1, 1, 1, 0.08)
                                               : rowBtn.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                                        Behavior on color { ColorAnimation { duration: 100 } }
                                    }

                                    Rectangle {
                                        visible: rowBtn.index < daySec.dayItems.length - 1
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: Theme.stroke
                                        opacity: 0.48
                                    }
                                }

                                contentItem: RowLayout {
                                    spacing: 16

                                    Label {
                                        Layout.preferredWidth: 50
                                        text: rowBtn.modelData.time ?? ""
                                        color: daySec.modelData.isToday ? Theme.accentText : Theme.muted
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        Accessible.ignored: true
                                    }

                                    // Crisp 40×56 poster with subtle bookmark ribbon when followed.
                                    Item {
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 56

                                        Poster {
                                            anchors.fill: parent
                                            source: rowBtn.modelData.posterUrl ?? ""
                                        }

                                        Image {
                                            visible: Boolean(rowBtn.modelData.inLibrary)
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.topMargin: -2
                                            anchors.rightMargin: 3
                                            width: 12
                                            height: 15
                                            sourceSize.width: Math.max(12, Math.ceil(12 * Screen.devicePixelRatio))
                                            sourceSize.height: Math.max(15, Math.ceil(15 * Screen.devicePixelRatio))
                                            smooth: true
                                            source: rowBtn.modelData.inLibrary
                                                    ? Theme.source(Theme.icon.bookmarkRibbon + "?color=2f81f7") : ""
                                            Accessible.ignored: true
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 3

                                        Label {
                                            id: rowTitle
                                            Layout.fillWidth: true
                                            text: rowBtn.modelData.title ?? ""
                                            color: rowBtn.hovered ? "#ffffff" : Theme.ink
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: (rowBtn.modelData.episode ? ("Ep. " + rowBtn.modelData.episode + "  ·  ") : "")
                                                  + (rowBtn.modelData.inLibrary ? "En tu biblioteca" : "Catálogo")
                                            color: Theme.muted
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }
                                    }

                                    ScheduleStateChip {
                                        id: rowChip
                                        status: rowBtn.modelData.state ?? ""
                                        label: rowBtn.modelData.stateLabel ?? ""
                                    }
                                }

                                FocusFrame {
                                    radiusInside: 6
                                    visible: rowBtn.activeFocus && InputModality.keyboard
                                }
                            }
                        }
                    }
                }
            }

            // Semana empty state.
            ColumnLayout {
                visible: root.scope === "week" && !root.loadingInitial && root.totalFilteredCount === 0
                Layout.fillWidth: true
                Layout.topMargin: 20
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    text: (root.followingOnly && root.totalUnfilteredCount > 0)
                          ? "No sigues ningún anime en emisión esta semana"
                          : "Sin emisiones esta semana"
                    color: Theme.muted
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                Button {
                    visible: root.followingOnly && root.totalUnfilteredCount > 0
                    text: "Mostrar todos"
                    onClicked: root.app.settings.scheduleFollowingOnly = false
                }
            }
        }
    }
}
