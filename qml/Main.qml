import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    readonly property color canvas: "#0b0b0d"
    readonly property color panel: "#121216"
    readonly property color line: "#29292f"
    readonly property color ink: "#f5f2ea"
    readonly property color muted: "#98969e"
    readonly property color accent: "#ff6b35"

    visible: true
    width: 1120
    height: 720
    minimumWidth: 880
    minimumHeight: 560
    title: qsTr("AnimeHub")
    color: canvas

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 248
            color: panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 0

                Label {
                    text: qsTr("ANIMEHUB")
                    color: ink
                    font.pixelSize: 17
                    font.weight: Font.Black
                    font.letterSpacing: 3
                }

                Label {
                    Layout.topMargin: 9
                    text: qsTr("Biblioteca personal")
                    color: muted
                    font.pixelSize: 12
                }

                ColumnLayout {
                    Layout.topMargin: 64
                    spacing: 8

                    Repeater {
                        model: [qsTr("Descubrir"), qsTr("Biblioteca"), qsTr("Descargas")]

                        delegate: Rectangle {
                            required property int index
                            required property string modelData

                            Layout.preferredWidth: 192
                            Layout.preferredHeight: 42
                            color: index === 0 ? "#201712" : "transparent"
                            border.color: index === 0 ? "#4a2a1e" : "transparent"
                            radius: 4

                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 14
                                text: modelData
                                color: index === 0 ? window.accent : window.muted
                                font.pixelSize: 13
                                font.weight: index === 0 ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    text: qsTr("FOUNDATION 0.1")
                    color: muted
                    font.family: "Consolas"
                    font.pixelSize: 10
                    font.letterSpacing: 1.6
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: line
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 56
                spacing: 0

                Label {
                    text: qsTr("BASE NATIVA")
                    color: accent
                    font.family: "Consolas"
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 2.4
                }

                Label {
                    Layout.topMargin: 18
                    Layout.maximumWidth: 680
                    text: qsTr("Tu anime.\nEn tus manos.")
                    color: ink
                    font.pixelSize: 68
                    font.weight: Font.Black
                    font.letterSpacing: -3
                    lineHeight: 0.84
                }

                Label {
                    Layout.topMargin: 28
                    Layout.maximumWidth: 570
                    text: qsTr("Una aplicación local para administrar biblioteca, reproducción y descargas sin convertir el escritorio en otra pestaña web.")
                    color: muted
                    wrapMode: Text.WordWrap
                    font.pixelSize: 16
                    lineHeight: 1.45
                }

                Rectangle {
                    Layout.topMargin: 42
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: line
                }

                RowLayout {
                    Layout.topMargin: 18
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("QT QUICK · C++26 · WINDOWS")
                        color: muted
                        font.family: "Consolas"
                        font.pixelSize: 10
                        font.letterSpacing: 1.4
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: qsTr("Scaffold listo").toUpperCase()
                        color: ink
                        font.family: "Consolas"
                        font.pixelSize: 10
                        font.letterSpacing: 1.4
                    }
                }
            }
        }
    }
}
