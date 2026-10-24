import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The head of the episode list: its title and count, the pages and, while episodes are being
// chosen, the page's check box. One line of fixed height: the box appearing moves no row.
RowLayout {
    id: root

    required property AnimeView view
    // A selection is under way, as the selection bar shows.
    property bool selecting: false

    signal moved()

    readonly property EpisodeModel episodes: view.episodes

    implicitHeight: 48
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

    CheckBox {
        id: selectPage
        visible: root.selecting && root.episodes.total > 2
        Layout.leftMargin: 16
        // A page is only worth naming when there are several.
        text: root.episodes.pageCount > 1 ? "Seleccionar esta página" : "Seleccionar todo"
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

    Item {
        Layout.fillWidth: true
    }

    Pager {
        visible: root.episodes.pageCount > 1
        episodes: root.episodes
        onMoved: root.moved()
    }
}
