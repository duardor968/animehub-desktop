import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The top of an anime's page: backdrop, poster, titles, main action and synopsis.
Item {
    id: root

    required property AnimeView view
    property string backText

    signal back()

    readonly property bool hasBackdrop: backdrop.status === Image.Ready
    implicitHeight: content.y + content.implicitHeight + 8

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

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0) }
                GradientStop { position: 0.55; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.5) }
                GradientStop { position: 1.0; color: Theme.canvas }
            }
        }
    }

    Button {
        id: backButton
        visible: root.backText !== ""
        x: 20
        y: 14
        flat: true
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
                Layout.preferredWidth: 150
                Layout.preferredHeight: 213
                Layout.alignment: Qt.AlignBottom
                source: root.view.posterUrl
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
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

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    visible: text !== ""
                    text: root.view.facts
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.view.genres
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.topMargin: 14
                    spacing: 8

                    Button {
                        id: primary
                        visible: root.view.primaryAction !== AnimeView.None
                        highlighted: root.view.primaryAction !== AnimeView.Wait
                        enabled: root.view.primaryAction !== AnimeView.Wait
                        text: root.view.primaryText
                        icon.source: Theme.source(root.view.primaryAction === AnimeView.Download
                                                  ? Theme.icon.download : Theme.icon.play)
                        onClicked: root.view.primary()
                    }

                    ColumnLayout {
                        visible: root.view.resumeProgress >= 0
                        spacing: 4
                        Layout.leftMargin: 4

                        ProgressLine {
                            Layout.preferredWidth: 140
                            value: root.view.resumeProgress
                        }

                        Label {
                            text: root.view.resumeText
                            color: Theme.muted
                            font.pixelSize: Theme.caption
                        }
                    }

                    Button {
                        visible: root.view.loaded && !root.view.inLibrary
                        text: "Añadir a la biblioteca"
                        icon.source: Theme.source(Theme.icon.add)
                        onClicked: root.view.setInLibrary(true)
                    }

                    ToolButton {
                        visible: root.view.loaded
                        checkable: true
                        checked: root.view.favorite
                        icon.source: Theme.source(checked ? Theme.icon.starFilled : Theme.icon.star)
                        icon.color: checked ? Theme.warning : Theme.ink
                        display: AbstractButton.IconOnly
                        text: "Favorito"
                        ToolTip.visible: hovered
                        ToolTip.text: checked ? "Quitar de favoritos" : "Añadir a favoritos"
                        onToggled: root.view.setFavorite(checked)
                    }

                    ToolButton {
                        id: moreButton
                        visible: root.view.loaded
                        icon.source: Theme.source(Theme.icon.more)
                        display: AbstractButton.IconOnly
                        text: "Más opciones"
                        ToolTip.visible: hovered
                        ToolTip.text: text
                        onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                    }
                }
            }
        }

        Label {
            id: synopsis
            property bool expanded: false
            Layout.fillWidth: true
            Layout.maximumWidth: 900
            visible: text !== ""
            text: root.view.synopsis
            color: Theme.ink
            opacity: 0.88
            font.pixelSize: Theme.body
            lineHeight: 1.25
            wrapMode: Text.Wrap
            maximumLineCount: expanded ? 1000 : 3
            elide: Text.ElideRight
        }

        Button {
            visible: synopsis.truncated || synopsis.expanded
            flat: true
            padding: 0
            text: synopsis.expanded ? "Mostrar menos" : "Mostrar más"
            onClicked: synopsis.expanded = !synopsis.expanded
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

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            visible: root.view.episodes.count > 0

            Label {
                text: "Episodios"
                color: Theme.ink
                font.pixelSize: Theme.subtitle
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }

            BusyIndicator {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                running: root.view.refreshing
                visible: running
            }
        }
    }

    Menu {
        id: moreMenu

        MenuItem {
            text: "Pendiente"
            checkable: true
            checked: root.view.pending
            onTriggered: root.view.setPending(checked)
        }
        MenuItem {
            text: "Avisar de episodios nuevos"
            checkable: true
            checked: root.view.notify
            enabled: root.view.inLibrary
            onTriggered: root.view.setNotify(checked)
        }
        MenuItem {
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
        MenuSeparator {}
        MenuItem {
            text: "Descargar los no vistos"
            icon.source: Theme.source(Theme.icon.download)
            onTriggered: root.view.downloadUnwatched()
        }
        MenuItem {
            text: "Marcar todo como visto"
            icon.source: Theme.source(Theme.icon.check)
            onTriggered: root.view.markAllWatched()
        }
        MenuItem {
            text: "Marcar todo como no visto"
            onTriggered: root.view.markAllUnwatched()
        }
        MenuItem {
            text: "Abrir la carpeta"
            icon.source: Theme.source(Theme.icon.folder)
            onTriggered: root.view.openFolder()
        }
        MenuSeparator { visible: root.view.inLibrary }
        MenuItem {
            text: "Quitar de la biblioteca"
            icon.source: Theme.source(Theme.icon.remove)
            visible: root.view.inLibrary
            height: visible ? implicitHeight : 0
            onTriggered: root.view.setInLibrary(false)
        }
    }

    Dialog {
        id: autoDownloadDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Descargar episodios nuevos"

        Label {
            width: 420
            wrapMode: Text.Wrap
            color: Theme.ink
            text: "Cuando salga un episodio nuevo de " + root.view.title
                  + ", AnimeHub lo descargará en " + root.view.downloadFolder
                  + ". Puedes desactivarlo en este mismo menú."
        }

        footer: DialogButtonBox {
            Button {
                text: "Activar"
                highlighted: true
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
