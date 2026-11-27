import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One episode, as a streaming app lists them: its still, then on two lines what it is and where
// it stands (when it came out, how far it was watched, on disk, its download), and one action
// in words for what comes next with it; the rest under ⋯, shown on hover or with the keyboard.
// Built on AbstractButton (not Fluent ItemDelegate) so mouse focus never leaves a stray left bar,
// and selected rows weld a 2 px #2f81f7 border around the 16:9 still with a soft row tint.
AbstractButton {
    id: row

    required property AnimeView view
    required property EpisodeMenu menu
    // The page is choosing episodes: every row shows its selection hint.
    property bool selecting: false
    required property int index
    required property real number
    required property string label
    required property string title
    required property string date
    required property url imageUrl
    required property int watchState
    required property real progress
    required property string progressText
    required property bool fresh
    required property bool hasFile
    required property string downloadState
    required property real downloadProgress
    required property string downloadText
    required property bool selected
    required property bool first

    readonly property bool watched: watchState === 2
    readonly property bool transferring: downloadState !== "" && downloadState !== "failed"
    readonly property bool rowHovered: row.hovered || still.containsMouse || mark.hovered
                                       || actionButton.hovered || more.hovered
    // "Visto · En disco", "8:12 de 24:00 · En disco"; nothing for an episode with nothing to it
    // yet. While it downloads, the download's own words follow.
    readonly property string status: {
        const parts = []
        if (watched)
            parts.push("Visto")
        else if (progressText !== "")
            parts.push(progressText)
        if (hasFile)
            parts.push("En disco")
        return parts.join(" · ")
    }
    // The one action in words (not `action`, AbstractButton's own): what its download needs
    // first, else watching or downloading it.
    readonly property string step: {
        switch (downloadState) {
        case "downloading":
        case "queued":
        case "waiting":
        case "resolving":
            return "pause"
        case "paused":
            return "resume"
        case "failed":
            return "retry"
        }
        return hasFile ? "play" : downloadState === "" ? "download" : ""
    }
    readonly property bool showsMore: rowHovered
                                      || (activeFocus && InputModality.keyboard)
                                      || (actionButton.activeFocus && InputModality.keyboard)
                                      || (more.activeFocus && InputModality.keyboard) || more.pressed

    function act() {
        switch (step) {
        case "play":
            view.play(number)
            break
        case "download":
            view.download(number)
            break
        case "pause":
            view.pauseDownload(number)
            break
        case "resume":
            view.resumeDownload(number)
            break
        case "retry":
            view.retryDownload(number)
            break
        }
    }

    function toggle(extend) {
        view.episodes.toggle(number, extend)
    }

    height: 88
    topPadding: 8
    bottomPadding: 8
    topInset: 2
    bottomInset: 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ListItem
    Accessible.name: label + (title ? ", " + title : "") + (date ? ", " + date : "")
                     + (status ? ", " + status : "") + (downloadText ? ", " + downloadText : "")
                     + (selected ? ", seleccionado" : "")
    onDoubleClicked: if (!selecting && hasFile) view.play(number)
    // While selecting, a click anywhere on the row toggles it; otherwise it takes mouse focus
    // (without keyboard highlight) so Space/Enter can act on it.
    onClicked: {
        if (ListView.view)
            ListView.view.currentIndex = index
        forceActiveFocus(Qt.MouseFocusReason)
        if (selecting)
            toggle(false)
    }
    Keys.onReturnPressed: if (!selecting && hasFile) view.play(number)
    Keys.onEnterPressed: if (!selecting && hasFile) view.play(number)
    // Space marks the episode, as its check box would; Mayús+Espacio extends the span.
    Keys.onSpacePressed: event => {
        toggle(event.modifiers & Qt.ShiftModifier)
        event.accepted = true
    }
    Keys.onMenuPressed: menu.show(row, more)

    // Subtle blue fill when selected (NO outer row border), quiet surface lift on hover.
    background: Rectangle {
        radius: Theme.radius
        color: row.selected
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                         row.rowHovered ? 0.14 : 0.09)
               : (row.down ? Qt.rgba(1, 1, 1, 0.07)
                  : row.rowHovered ? Qt.rgba(1, 1, 1, 0.045) : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    // Keyboard focus frame only while navigating by keyboard.
    FocusFrame {
        target: row.background
        visible: row.activeFocus && InputModality.keyboard
        radiusInside: Theme.radius
    }

    // Rows apart by a hairline under the content's column, as a list of episodes is drawn.
    Rectangle {
        x: row.leftPadding
        width: row.width - row.leftPadding - row.rightPadding
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
        opacity: 0.6
    }

    contentItem: RowLayout {
        spacing: 16

        Item {
            // 16:9, as the episodes' stills.
            Layout.preferredWidth: 128
            Layout.preferredHeight: 72
            clip: true

            Poster {
                anchors.fill: parent
                source: row.imageUrl
                opacity: row.watched ? 0.5 : 1
            }

            // How far it was watched, along the still's foot.
            ProgressLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: row.progress >= 0
                value: row.progress
                radius: 0
            }

            // On disk (and not selecting), a press on the still plays it; otherwise it falls to the row.
            MouseArea {
                id: still
                anchors.fill: parent
                enabled: !row.selecting && row.hasFile
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: row.view.play(row.number)
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.5)
                opacity: still.enabled && still.containsMouse ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 120 } }

                Glyph {
                    anchors.centerIn: parent
                    glyph: Theme.icon.play
                    size: 22
                    color: "white"
                }
            }

            // Clean corner selection mark: invisible at rest, translucent check on hover, solid
            // #2f81f7 welded into the thumbnail border once selected.
            SelectionMark {
                id: mark
                anchors.top: parent.top
                anchors.right: parent.right
                size: 42
                checked: row.selected
                selecting: row.selecting
                rowHovered: row.rowHovered || (row.activeFocus && InputModality.keyboard)
                Accessible.name: "Seleccionar " + row.label.toLowerCase()
                // The row takes the focus, so Esc and Space reach the page from here.
                onChosen: extend => {
                    row.forceActiveFocus(Qt.MouseFocusReason)
                    row.toggle(extend)
                }
            }

            // Continuous 2 px #2f81f7 inset border seamlessly welded into SelectionMark's corner.
            Rectangle {
                anchors.fill: parent
                visible: row.selected
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
            }
        }

        // What it is, then where it stands; never narrower than "Episodio 12 Nuevo".
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 120
            Layout.alignment: Qt.AlignVCenter
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    text: row.title !== "" ? row.label + " · " + row.title : row.label
                    color: row.watched ? Theme.muted : Theme.ink
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    ToolTip.visible: row.rowHovered && truncated
                    ToolTip.delay: 600
                    ToolTip.text: text
                }

                Label {
                    visible: row.fresh
                    text: "Nuevo"
                    color: Theme.accentText
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            // Kept when empty: every title sits at one height. The check sits directly beside "Visto".
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    visible: row.date !== ""
                    text: row.date + (row.watched || row.progressText !== "" || row.hasFile ? " ·" : "")
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                }

                Glyph {
                    visible: row.watched
                    glyph: Theme.icon.check
                    size: 12
                    color: Theme.success
                }

                Label {
                    visible: row.status !== ""
                    Layout.fillWidth: row.downloadText === ""
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    text: row.status
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                }

                Label {
                    visible: row.downloadText !== ""
                    Layout.fillWidth: true
                    text: (row.date !== "" || row.status !== "" ? "· " : "") + row.downloadText
                    color: row.downloadState === "failed" ? Theme.danger : Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                }
            }

            // Fluent's bar while it transfers, no longer than a glance needs.
            ProgressBar {
                Layout.fillWidth: true
                Layout.maximumWidth: 260
                Layout.topMargin: 2
                visible: row.transferring
                from: 0
                to: 1
                value: row.downloadProgress
                indeterminate: row.downloadState === "resolving" && width > 0
                Accessible.name: row.downloadText
            }
        }

        // While choosing, a press on the row chooses: its own actions step aside, keeping their
        // place so nothing moves. Fixed action slot left-aligns every row icon on one crisp axis.
        RowLayout {
            spacing: 4
            opacity: row.selecting ? 0 : 1
            enabled: !row.selecting

            Item {
                Layout.preferredWidth: 118
                Layout.preferredHeight: Math.max(32, actionButton.implicitHeight)
                Layout.alignment: Qt.AlignVCenter

                Button {
                    id: actionButton
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    // Hug content so short labels ("Ver") do not get centered inside Fluent's
                    // wider default minimum width and drift right of "Descargar" / "Pausar".
                    width: contentItem ? Math.ceil(contentItem.implicitWidth) + leftPadding + rightPadding
                                       : implicitWidth
                    visible: row.step !== ""
                    flat: true
                    leftPadding: 10
                    rightPadding: 10
                    text: ({ play: "Ver", download: "Descargar", pause: "Pausar", resume: "Reanudar",
                             retry: "Reintentar" })[row.step] ?? ""
                    // Play is for watching: resuming a download keeps the download glyph.
                    icon.source: Theme.source(({ play: Theme.icon.play, download: Theme.icon.download,
                                                 pause: Theme.icon.pause, resume: Theme.icon.download,
                                                 retry: Theme.icon.retry })[row.step] ?? Theme.icon.more)
                    Accessible.name: ({ play: "Ver " + row.label.toLowerCase(),
                                        download: "Descargar " + row.label.toLowerCase(),
                                        pause: "Pausar la descarga", resume: "Reanudar la descarga",
                                        retry: "Reintentar la descarga" })[row.step] ?? ""
                    onClicked: row.act()
                }
            }

            // Kept in its place when hidden, so the action never moves under the pointer.
            ToolButton {
                id: more
                opacity: row.showsMore ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                text: "Opciones de " + row.label.toLowerCase()
                ToolTip.visible: hovered
                ToolTip.text: "Más opciones"
                onClicked: row.menu.show(row, more)
            }
        }
    }
}
