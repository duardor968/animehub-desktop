import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// One episode: what it is, how far it was watched, and what can be done with it.
ItemDelegate {
    id: row

    required property AnimeView view
    required property EpisodeMenu menu
    // The page is choosing episodes: every row shows its check box.
    property bool selecting: false
    required property int index
    required property real number
    required property string label
    required property string title
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
    // "Visto · En disco", "8:12 de 24:00 · En disco"
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

    function toggle(extend) {
        view.episodes.toggle(number, extend)
    }

    height: 76
    leftPadding: 32
    rightPadding: 24
    // The hover and focus marks start next to the content, not at the panel's edge.
    leftInset: 16
    rightInset: 8
    Accessible.name: label + (title ? ", " + title : "") + (status ? ", " + status : "")
                     + (selected ? ", seleccionado" : "")
    onDoubleClicked: if (hasFile) view.play(number)
    // The clicked row takes the keyboard: Space marks it, Enter plays it.
    onClicked: {
        ListView.view.currentIndex = index
        forceActiveFocus()
    }
    Keys.onReturnPressed: if (hasFile) view.play(number)
    // Space marks the episode, as its check box would; Mayús+Espacio extends the span.
    Keys.onSpacePressed: event => {
        toggle(event.modifiers & Qt.ShiftModifier)
        event.accepted = true
    }

    contentItem: RowLayout {
        spacing: 16

        Item {
            Layout.preferredWidth: 104
            Layout.preferredHeight: 58

            Poster {
                anchors.fill: parent
                source: row.imageUrl
                opacity: row.watched ? 0.5 : 1
            }

            ProgressLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: row.progress >= 0
                value: row.progress
                radius: 0
            }

            // On the thumbnail's corner, as on the web; shown on hover or while choosing.
            CheckBox {
                id: box
                x: 4
                y: 4
                padding: 0
                focusPolicy: Qt.NoFocus
                checked: row.selected
                opacity: row.selected || row.selecting || row.hovered ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
                Accessible.name: "Seleccionar " + row.label.toLowerCase()
                // Keyboard and screen readers toggle the box itself.
                onToggled: {
                    row.toggle(false)
                    checked = Qt.binding(() => row.selected)
                }

                // Mayús+clic marks a span: a click's modifiers do not reach the box's signal.
                // The row takes the focus, so Esc and Space reach the page from here.
                MouseArea {
                    anchors.fill: parent
                    onClicked: mouse => {
                        row.forceActiveFocus()
                        row.toggle(mouse.modifiers & Qt.ShiftModifier)
                    }
                }
            }
        }

        // Takes what the download status leaves; never narrower than "Episodio 12 Nuevo".
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 120
            Layout.preferredWidth: 120
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: row.label
                    color: row.watched ? Theme.muted : Theme.ink
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                }

                Label {
                    visible: row.fresh
                    text: "Nuevo"
                    color: Theme.accent
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: row.title
                color: Theme.muted
                elide: Text.ElideRight
                font.pixelSize: Theme.caption
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: row.status !== ""

                Glyph {
                    visible: row.watched
                    glyph: Theme.icon.check
                    size: 12
                    color: Theme.success
                }

                Label {
                    Layout.fillWidth: true
                    text: row.status
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                }
            }
        }

        // As wide as its text (up to a limit), narrower and elided when the row is short:
        // the actions on the right keep their place.
        ColumnLayout {
            visible: row.downloadState !== ""
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            Layout.preferredWidth: Layout.maximumWidth
            Layout.maximumWidth: Math.max(160, Math.min(downloadLabel.implicitWidth, 360))
            spacing: 4

            ProgressLine {
                Layout.fillWidth: true
                visible: row.transferring
                value: row.downloadProgress
            }

            Label {
                id: downloadLabel
                Layout.fillWidth: true
                text: row.downloadText
                color: row.downloadState === "failed" ? Theme.danger : Theme.muted
                font.pixelSize: Theme.caption
                elide: Text.ElideRight
            }
        }

        ToolButton {
            visible: row.hasFile
            icon.source: Theme.source(Theme.icon.play)
            display: AbstractButton.IconOnly
            text: "Ver " + row.label.toLowerCase()
            ToolTip.visible: hovered
            ToolTip.text: "Ver"
            onClicked: row.view.play(row.number)
        }

        ToolButton {
            visible: !row.hasFile && row.downloadState === ""
            icon.source: Theme.source(Theme.icon.download)
            display: AbstractButton.IconOnly
            text: "Descargar " + row.label.toLowerCase()
            ToolTip.visible: hovered
            ToolTip.text: "Descargar"
            onClicked: row.view.download(row.number)
        }

        ToolButton {
            visible: row.downloadState === "downloading" || row.downloadState === "queued"
                     || row.downloadState === "waiting" || row.downloadState === "resolving"
            icon.source: Theme.source(Theme.icon.pause)
            display: AbstractButton.IconOnly
            text: "Pausar la descarga"
            ToolTip.visible: hovered
            ToolTip.text: "Pausar"
            onClicked: row.view.pauseDownload(row.number)
        }

        ToolButton {
            visible: row.downloadState === "paused"
            // Play is for watching: resuming a download keeps the download glyph.
            icon.source: Theme.source(Theme.icon.download)
            display: AbstractButton.IconOnly
            text: "Reanudar la descarga"
            ToolTip.visible: hovered
            ToolTip.text: "Reanudar"
            onClicked: row.view.resumeDownload(row.number)
        }

        ToolButton {
            visible: row.downloadState === "failed"
            icon.source: Theme.source(Theme.icon.retry)
            display: AbstractButton.IconOnly
            text: "Reintentar la descarga"
            ToolTip.visible: hovered
            ToolTip.text: "Reintentar"
            onClicked: row.view.retryDownload(row.number)
        }

        ToolButton {
            id: more
            icon.source: Theme.source(Theme.icon.more)
            display: AbstractButton.IconOnly
            text: "Opciones de " + row.label.toLowerCase()
            ToolTip.visible: hovered
            ToolTip.text: "Más opciones"
            onClicked: row.menu.show(row, more)
        }
    }
}
