import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The head of the episode list: its title and count, the page's check box and the pages.
ColumnLayout {
    id: root

    required property AnimeView view

    signal moved()

    readonly property EpisodeModel episodes: view.episodes

    spacing: 10

    RowLayout {
        spacing: 8

        Label {
            text: "Episodios"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }

        Label {
            text: "(" + root.episodes.total + ")"
            color: Theme.muted
            font.pixelSize: Theme.subtitle
        }

        BusyIndicator {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            running: root.view.refreshing
            visible: running
        }
    }

    RowLayout {
        Layout.fillWidth: true
        // From the model, not from its children: they are hidden while it is.
        visible: root.episodes.total > 2 || root.episodes.pageCount > 1
        spacing: 8

        CheckBox {
            id: selectPage
            visible: root.episodes.total > 2
            // The box on the column's edge, under the title; Fluent pads it.
            Layout.leftMargin: -leftPadding
            text: "Seleccionar esta página"
            tristate: true
            checkState: root.episodes.pageSelection
            // A click selects the whole page unless all of it already is.
            nextCheckState: () => root.episodes.pageSelection === Qt.Checked ? Qt.Unchecked
                                                                              : Qt.Checked
            onClicked: {
                root.episodes.selectPage(checkState === Qt.Checked)
                checkState = Qt.binding(() => root.episodes.pageSelection)
            }
        }

        Item { Layout.fillWidth: true }

        Pager {
            id: pager
            visible: root.episodes.pageCount > 1
            episodes: root.episodes
            onMoved: root.moved()
        }
    }
}
