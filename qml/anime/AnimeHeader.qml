import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The top of an anime's page: backdrop, poster, titles, facts, the actions and the synopsis.
Item {
    id: root

    required property AnimeView view
    property string backText

    signal back()
    signal chooseRange()
    signal chooseEpisodes()
    // A film has no episode list: its position is typed from here.
    signal askPosition()

    readonly property bool hasBackdrop: backdrop.status === Image.Ready
    // The actions leave the column beside the poster when they no longer fit in one line there.
    readonly property bool actionsBelow: primaryActions.implicitWidth + actionRow.spacing
                                         + secondaryActions.implicitWidth > titles.width
    // The row of actions, for the compact bar to appear once it scrolls away.
    readonly property Item actions: actionRow
    implicitHeight: content.y + content.implicitHeight

    Image {
        id: backdrop
        anchors.left: parent.left
        anchors.right: parent.right
        height: 300
        source: root.view.backdropUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        opacity: status === Image.Ready ? 0.55 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // A sibling, not a child: it must not inherit the image's opacity to reach the canvas.
    Rectangle {
        anchors.fill: backdrop
        visible: root.hasBackdrop
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0) }
            GradientStop { position: 0.6; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.45) }
            GradientStop { position: 1.0; color: Theme.canvas }
        }
    }

    Button {
        id: backButton
        visible: root.backText !== ""
        x: 20
        y: 14
        flat: true
        // Sized to its content: Fluent's minimum width would centre a short label and shift
        // the arrow off the poster's edge.
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        text: root.backText
        icon.source: Theme.source(Theme.icon.back)
        onClicked: root.back()
        Accessible.name: "Volver a " + root.backText
    }

    ColumnLayout {
        id: content
        x: 32
        y: root.hasBackdrop ? 150 : (backButton.visible ? 64 : 28)
        width: root.width - 64
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 24

            Poster {
                id: poster
                Layout.preferredWidth: 150
                Layout.preferredHeight: 213
                Layout.alignment: Qt.AlignTop
                source: root.view.posterUrl
            }

            ColumnLayout {
                id: titles
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                // Level with the poster's bottom when it fits beside it; from its top otherwise.
                Layout.alignment: implicitHeight > poster.height ? Qt.AlignTop : Qt.AlignBottom
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: root.view.title
                    color: Theme.ink
                    font.pixelSize: Theme.title
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.view.alternativeTitle
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    elide: Text.ElideRight
                }

                // "★ 9,1 (48 mil) · TV Anime · 2023 · Finalizado · 28 episodios": one text, so a
                // narrow column wraps it between words instead of cutting it.
                Label {
                    readonly property string star: "<span style=\"font-family:'" + Theme.iconFont
                                                    + "'; font-size:13px\">" + Theme.glyph(Theme.icon.starFilled)
                                                    + "</span>"
                    readonly property string score: root.view.score === "" ? ""
                        : "<span style=\"color:" + Theme.ink + "; font-weight:600\">" + star + "&nbsp;"
                          + root.view.score + "</span>" + (root.view.facts !== "" ? " · " : "")

                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    visible: root.view.score !== "" || root.view.facts !== ""
                    textFormat: Text.RichText
                    text: score + root.view.facts.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    wrapMode: Text.Wrap
                    Accessible.name: (root.view.score !== "" ? "Puntuación " + root.view.score + ". " : "")
                                     + root.view.facts
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.view.genres
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    visible: root.view.nextAiring !== ""
                    spacing: 8

                    Glyph {
                        glyph: Theme.icon.clock
                        size: 14
                        color: Theme.muted
                    }

                    Label {
                        text: "Próximo episodio: " + root.view.nextAiring
                        color: Theme.ink
                        font.pixelSize: Theme.body
                    }
                }

                // Where the actions go while they fit beside the poster.
                Item {
                    id: besideSlot
                    Layout.topMargin: 14
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: actionRow.implicitHeight
                    visible: !root.actionsBelow
                }
            }
        }

        // ...and once they do not: the whole width under the poster.
        Item {
            id: belowSlot
            Layout.fillWidth: true
            implicitHeight: actionRow.implicitHeight
            visible: root.actionsBelow
        }

        Label {
            id: synopsis
            property bool expanded: false
            // Collapsed, the first paragraph: it usually ends a sentence, so no "….".
            readonly property var paragraphs: root.view.synopsis.split(/\n\s*\n/)
            // Each anime opens collapsed.
            onParagraphsChanged: expanded = false
            Layout.fillWidth: true
            Layout.maximumWidth: 900
            visible: text !== ""
            text: expanded ? root.view.synopsis : paragraphs[0]
            color: Theme.ink
            opacity: 0.88
            font.pixelSize: Theme.body
            lineHeight: 1.25
            wrapMode: Text.Wrap
            maximumLineCount: expanded ? 1000 : 4
            elide: Text.ElideRight
        }

        Button {
            id: more
            visible: synopsis.expanded || synopsis.truncated || synopsis.paragraphs.length > 1
            flat: true
            text: synopsis.expanded ? "Mostrar menos" : "Mostrar más"
            onClicked: synopsis.expanded = !synopsis.expanded
            // Its text in the synopsis column; the hover background may overhang, as in WinUI.
            Layout.leftMargin: -leftPadding
            Layout.topMargin: -14
        }

        RowLayout {
            visible: root.view.problem !== ""
            spacing: 10

            Glyph {
                glyph: Theme.icon.warning
                color: Theme.warning
            }

            Label {
                text: root.view.problem
                color: Theme.ink
            }

            Button {
                text: "Reintentar"
                onClicked: root.view.reload()
            }
        }
    }

    // The main action and the rest; beside the poster while they fit there, under it otherwise.
    // Each group wraps as one.
    Flow {
        id: actionRow
        parent: root.actionsBelow ? belowSlot : besideSlot
        width: parent.width
        spacing: 8

        PrimaryActions {
            id: primaryActions
            view: root.view
            onChooseRange: root.chooseRange()
            onChooseEpisodes: root.chooseEpisodes()
        }

        Row {
            id: secondaryActions
            spacing: 8

            Button {
                visible: root.view.trailerUrl.toString() !== ""
                text: "Tráiler"
                icon.source: Theme.source("e8a7")
                Accessible.name: "Ver el tráiler en el navegador"
                onClicked: root.view.openTrailer()
            }

            // Being in the library is a state: pressing it takes the anime out.
            Button {
                visible: root.view.loaded
                text: root.view.inLibrary ? "En la biblioteca" : "Añadir a la biblioteca"
                icon.source: Theme.source(root.view.inLibrary ? Theme.icon.check : Theme.icon.add)
                Accessible.checkable: true
                Accessible.checked: root.view.inLibrary
                ToolTip.visible: hovered && root.view.inLibrary
                ToolTip.text: "Quitar de la biblioteca"
                onClicked: root.view.setInLibrary(!root.view.inLibrary)
            }

            ToolButton {
                visible: root.view.loaded
                icon.source: Theme.source(root.view.favorite ? Theme.icon.starFilled
                                                             : Theme.icon.star)
                icon.color: root.view.favorite ? Theme.warning : Theme.ink
                display: AbstractButton.IconOnly
                text: root.view.favorite ? "Quitar de favoritos" : "Añadir a favoritos"
                Accessible.checkable: true
                Accessible.checked: root.view.favorite
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: root.view.setFavorite(!root.view.favorite)
            }

            ToolButton {
                id: moreButton
                visible: root.view.loaded
                icon.source: Theme.source(Theme.icon.more)
                display: AbstractButton.IconOnly
                text: "Más opciones"
                ToolTip.visible: hovered
                ToolTip.text: text
                onClicked: {
                    moreMenu.folderExists = root.view.hasFolder()
                    moreMenu.show(moreButton)
                }
            }
        }
    }

    OptionsMenu {
        id: moreMenu

        // Checked when the menu opens: the folder appears with the first download.
        property bool folderExists: false

        entries: [notifyEntry, autoEntry, airingSeparator, watchedEntry, unwatchedEntry,
                  positionEntry, filesSeparator, cancelEntry, sendEntry, trashWatchedEntry,
                  trashFilmEntry, folderEntry]

        MenuEntry {
            id: notifyEntry
            property bool wanted: root.view.airing
            text: "Avisar de episodios nuevos"
            checkable: true
            checked: root.view.notify
            enabled: root.view.inLibrary
            onTriggered: root.view.setNotify(checked)
        }
        MenuEntry {
            id: autoEntry
            property bool wanted: root.view.airing
            text: "Descargar episodios nuevos"
            checkable: true
            checked: root.view.autoDownload
            onTriggered: {
                if (checked) {
                    checked = false // the user confirms first
                    autoDownloadDialog.open()
                } else {
                    root.view.setAutoDownload(false)
                }
            }
        }
        MenuSeparator {
            id: airingSeparator
            property bool separator: true
        }
        MenuEntry {
            id: watchedEntry
            property bool wanted: root.view.watchedProgress < 1
            text: root.view.single ? "Marcar como visto" : "Marcar todo como visto"
            onTriggered: root.view.markAllWatched()
        }
        MenuEntry {
            id: unwatchedEntry
            property bool wanted: root.view.watchedProgress > 0 || root.view.nextProgress >= 0
            text: root.view.single ? "Marcar como no visto" : "Marcar todo como no visto"
            onTriggered: root.view.markAllUnwatched()
        }
        MenuEntry {
            id: positionEntry
            property bool wanted: root.view.single
            text: "Continuar desde…"
            glyph: Theme.icon.clock
            onTriggered: root.askPosition()
        }
        MenuSeparator {
            id: filesSeparator
            property bool separator: true
        }
        MenuEntry {
            id: cancelEntry
            property bool wanted: root.view.hasDownloads
            text: "Cancelar la descarga"
            glyph: Theme.icon.cancel
            onTriggered: root.view.cancelSeries(false)
        }
        MenuEntry {
            id: sendEntry
            property bool wanted: root.view.single && root.view.canSendToJDownloader
                                  && root.view.diskText === ""
            text: "Enviar a JDownloader"
            glyph: Theme.icon.send
            onTriggered: root.view.sendToJDownloader(root.view.firstNumber)
        }
        MenuEntry {
            id: trashWatchedEntry
            property bool wanted: root.view.watchedOnDisk && !root.view.single
            text: "Mover los vistos a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: root.view.trashWatched()
        }
        MenuEntry {
            id: trashFilmEntry
            property bool wanted: root.view.single && root.view.diskText !== ""
            text: "Mover a la Papelera"
            glyph: Theme.icon.trash
            onTriggered: root.view.trashFile(root.view.firstNumber)
        }
        MenuEntry {
            id: folderEntry
            property bool wanted: moreMenu.folderExists
            text: "Abrir la carpeta"
            glyph: Theme.icon.folder
            onTriggered: root.view.openFolder()
        }
    }

    ModalDialog {
        id: autoDownloadDialog
        width: 480
        title: "Descargar episodios nuevos"

        // Fixed widths on both sides: Fluent's Dialog loops when its content follows it.
        Label {
            width: 432
            wrapMode: Text.Wrap
            color: Theme.ink
            text: "Cuando salga un episodio nuevo de " + root.view.title
                  + ", AnimeHub lo descargará en " + root.view.downloadFolder
                  + ". Puedes desactivarlo en este mismo menú."
        }

        footer: DialogButtonBox {
            Button {
                text: "Activar"
                highlighted: autoDownloadDialog.visible // DialogButtonBox clears it while it builds
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: "Cancelar"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: root.view.setAutoDownload(true)
    }
}
