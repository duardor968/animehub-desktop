import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The band under a page's header, as Steam's under a game: the main action on the left, the
// figures beside it and the page's own buttons on the right. The page scrolls it with the header
// until it reaches the top, where it stays, led there by the poster and title it stands for.
Rectangle {
    id: root

    // [{caption, value, progress (< 0: no bar), problem}], in the order they give way: a figure
    // shows whole or not at all, and those after one that does not fit go with it.
    property var figures: []
    // Stuck to the top: a hairline parts it from what scrolls under it.
    property bool pinned: false
    // What the page is about, shown once the header that says it has scrolled away.
    property string title
    property url posterUrl
    // The band spans the page; its content keeps to the page's column from the left margin.
    property real contentWidth: width - 2 * Theme.section
    // The main action and what goes with it.
    default property alias actions: leading.data
    // The page's own buttons.
    property alias trailing: trailingRow.data

    implicitHeight: 72
    color: Theme.pane
    Accessible.role: Accessible.ToolBar

    // A caption over its value, as Steam lays out its figures; a bar under it for a share.
    component Figure: ColumnLayout {
        id: figure

        required property var modelData

        spacing: 3
        Accessible.role: Accessible.StaticText
        Accessible.name: modelData.caption + ": " + modelData.value

        Label {
            text: figure.modelData.caption
            color: Theme.muted
            font.pixelSize: Theme.caption
        }

        Label {
            text: figure.modelData.value
            color: figure.modelData.problem ? Theme.danger : Theme.ink
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
        }

        ProgressLine {
            Layout.fillWidth: true
            Layout.topMargin: 3
            // Wide enough to read as a share next to a short value like "12/13".
            implicitWidth: 96
            visible: figure.modelData.progress >= 0
            value: figure.modelData.progress
        }
    }

    // The bar covers the page: presses on its empty parts must not reach what is under it.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = false
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
        visible: root.pinned
    }

    RowLayout {
        x: Theme.section
        width: root.contentWidth
        height: parent.height
        spacing: 0

        // Pinned, what the bar acts on; it opens from the left edge, easing the button aside.
        Item {
            id: identity

            property real shown: root.pinned ? 1 : 0
            Behavior on shown { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            Layout.preferredWidth: (identityRow.implicitWidth + 24) * shown
            Layout.fillHeight: true
            visible: shown > 0
            opacity: shown
            clip: true

            RowLayout {
                id: identityRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Poster {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 42
                    source: root.posterUrl
                }

                Label {
                    visible: text !== ""
                    Layout.maximumWidth: 200
                    text: root.title
                    color: Theme.ink
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // Layouts fill by default: these two keep their content's width, the figures take the rest.
        RowLayout {
            id: leading
            Layout.fillWidth: false
            spacing: 8
        }

        Item {
            id: figureArea
            // Apart from the actions, or on the edge where there are none.
            Layout.leftMargin: leading.implicitWidth > 0 ? 28 : 0
            Layout.rightMargin: 16
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0

            Row {
                id: figureRow

                // Whether the figures up to `index` fit side by side; measured from their
                // texts, so it holds before the figures exist.
                function fits(index) {
                    let used = 0
                    for (let i = 0; i <= index && i < root.figures.length; ++i) {
                        const figure = root.figures[i]
                        const width = Math.max(captionMetrics.advanceWidth(figure.caption),
                                               valueMetrics.advanceWidth(figure.value),
                                               figure.progress >= 0 ? 96 : 0)
                        used += (i > 0 ? spacing : 0) + Math.ceil(width) + 2
                    }
                    return used <= figureArea.width
                }

                anchors.verticalCenter: parent.verticalCenter
                spacing: 28

                Repeater {
                    model: root.figures

                    delegate: Figure {
                        required property int index
                        visible: figureRow.fits(index)
                    }
                }
            }

            // The figures' fonts, from a label of the style.
            Label {
                id: probe
                visible: false
            }

            FontMetrics {
                id: captionMetrics
                font.family: probe.font.family
                font.pixelSize: Theme.caption
            }

            FontMetrics {
                id: valueMetrics
                font.family: probe.font.family
                font.pixelSize: Theme.body
                font.weight: Font.DemiBold
            }
        }

        RowLayout {
            id: trailingRow
            Layout.fillWidth: false
            spacing: 4
        }
    }
}
