import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// "Tu progreso": the episode to watch next, how much is watched, what is on disk and the
// series' downloads. Type and space, no box: only the next episode, a unit that responds to
// the pointer, gets a surface on hover.
ColumnLayout {
    id: root

    required property AnimeView view

    // The main button already follows the downloads.
    readonly property bool mainShowsDownloads: view.primaryAction === AnimeView.Downloading
                                               || view.primaryAction === AnimeView.Paused
    readonly property bool showsDownloads: (view.downloadRunning || view.downloadPaused)
                                           && !mainShowsDownloads

    spacing: 14

    // A caption over its value, as Steam lays out its figures.
    component Figure: ColumnLayout {
        property alias caption: captionLabel.text
        property alias value: valueLabel.text

        spacing: 4

        Label {
            id: captionLabel
            color: Theme.muted
            font.pixelSize: Theme.caption
        }

        Label {
            id: valueLabel
            color: Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
        }
    }

    Label {
        text: "Tu progreso"
        color: Theme.ink
        font.pixelSize: Theme.subtitle
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
    }

    // Every piece hangs from the same top line: captions level with one another.
    Flow {
        Layout.fillWidth: true
        spacing: Theme.section

        AbstractButton {
            id: next
            visible: !root.view.single && root.view.nextNumber >= 0
            enabled: root.view.nextHasFile || !root.view.nextBusy
            implicitWidth: 330
            implicitHeight: Math.max(72, contentItem.implicitHeight)
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            // The hover surface overhangs; the thumbnail stays on the column's edge.
            leftInset: -8
            rightInset: -8
            topInset: -8
            bottomInset: -8
            Accessible.name: (root.view.nextHasFile ? "Ver " : "Descargar ") + root.view.nextTitle
            onClicked: root.view.openNext()

            background: Rectangle {
                radius: Theme.radius
                color: next.hovered || next.visualFocus ? Theme.hover : "transparent"
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            contentItem: RowLayout {
                spacing: 14

                Item {
                    Layout.preferredWidth: 128
                    Layout.preferredHeight: 72

                    Poster {
                        anchors.fill: parent
                        source: root.view.nextImage
                    }

                    ProgressLine {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        visible: root.view.nextProgress >= 0
                        value: root.view.nextProgress
                        radius: 0
                    }

                    // What pressing it does, shown on hover.
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: Qt.rgba(0, 0, 0, 0.45)
                        opacity: next.hovered && next.enabled ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }

                        Glyph {
                            anchors.centerIn: parent
                            glyph: root.view.nextHasFile ? Theme.icon.play : Theme.icon.download
                            size: 22
                            color: "white"
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 4

                    Label {
                        text: "Siguiente"
                        color: Theme.muted
                        font.pixelSize: Theme.caption
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.view.nextTitle
                        color: Theme.ink
                        font.pixelSize: Theme.body
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.view.nextState
                        color: root.view.nextFailed ? Theme.danger : Theme.muted
                        font.pixelSize: Theme.caption
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Figure {
            caption: root.view.single ? "Visionado" : "Vistos"
            value: root.view.watchedText

            ProgressLine {
                Layout.preferredWidth: 140
                Layout.topMargin: 4
                value: root.view.watchedProgress
            }
        }

        Figure {
            visible: root.view.diskText !== ""
            caption: "En disco"
            value: root.view.diskText
        }

        // The series' downloads, when the main button shows something else, and any failure.
        ColumnLayout {
            visible: root.showsDownloads || root.view.failedCount > 0
            spacing: 4

            Label {
                // With the main button on the downloads, only their failures are left here.
                text: !root.showsDownloads ? "Descargas"
                      : root.view.downloadWaiting ? "En espera"
                      : root.view.downloadRunning ? "Descargando"
                      : root.view.downloadPaused ? "En pausa" : "Descargas"
                color: Theme.muted
                font.pixelSize: Theme.caption
            }

            RowLayout {
                visible: root.showsDownloads
                spacing: 12

                ColumnLayout {
                    spacing: 8

                    Label {
                        text: root.view.downloadText
                        color: Theme.ink
                        font.pixelSize: Theme.body
                        font.weight: Font.DemiBold
                    }

                    ProgressLine {
                        Layout.preferredWidth: 140
                        value: root.view.primaryProgress
                        Behavior on value { NumberAnimation { duration: 250 } }
                    }
                }

                ToolButton {
                    icon.source: Theme.source(root.view.downloadPaused ? Theme.icon.download
                                                                       : Theme.icon.pause)
                    display: AbstractButton.IconOnly
                    text: root.view.downloadPaused ? "Reanudar" : "Pausar"
                    Accessible.name: text + " las descargas de " + root.view.title
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: root.view.downloadPaused ? root.view.resumeSeries()
                                                        : root.view.pauseSeries()
                }
            }

            RowLayout {
                visible: root.view.failedCount > 0
                spacing: 8

                Glyph {
                    glyph: Theme.icon.warning
                    size: 14
                    color: Theme.danger
                }

                Label {
                    text: root.view.failedCount + " con error"
                    color: Theme.danger
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                }

                Button {
                    flat: true
                    text: "Reintentar fallidos"
                    onClicked: root.view.retryFailed()
                }
            }
        }
    }
}
