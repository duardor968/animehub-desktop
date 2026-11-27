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

    height: 48
    Layout.preferredHeight: 48
    Layout.minimumHeight: 48
    Layout.maximumHeight: 48
    spacing: 8

    Label {
        Layout.alignment: Qt.AlignVCenter
        text: "Episodios"
        color: Theme.ink
        font.pixelSize: Theme.subtitle
        font.weight: Font.DemiBold
        Accessible.role: Accessible.Heading
    }

    Label {
        Layout.alignment: Qt.AlignVCenter
        text: "(" + root.episodes.total + ")"
        color: Theme.muted
        font.pixelSize: Theme.subtitle
    }

    BusyIndicator {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: 20
        Layout.preferredHeight: 20
        running: root.view.refreshing
        visible: running
    }

    CheckBox {
        id: selectPage
        Layout.alignment: Qt.AlignVCenter
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
        Layout.alignment: Qt.AlignVCenter
        visible: root.episodes.pageCount > 1
        episodes: root.episodes
        onMoved: root.moved()
    }
}
