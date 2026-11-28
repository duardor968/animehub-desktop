import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The head of the episode list: its title and count, the pages and, while episodes are being
// chosen, the page's check box. Stays one 48 px line whenever everything fits; on narrow panes
// (e.g. 820x600 with >50 episodes while selecting), the check box wraps cleanly to a second line
// so neither the heading nor the pager ever clips.
ColumnLayout {
    id: root

    required property AnimeView view
    // A selection is under way, as the selection bar shows.
    property bool selecting: false

    signal moved()

    readonly property EpisodeModel episodes: view.episodes
    readonly property bool showSelect: root.selecting && root.episodes.total > 2
    readonly property real selectNeeded: Math.max(inlineSelect.implicitWidth, wrappedSelect.implicitWidth)
    readonly property real topRowNeeded: headingLabel.implicitWidth + countLabel.implicitWidth
                                         + (busyIndicator.visible ? 28 : 0)
                                         + (showSelect ? selectNeeded + 24 : 0)
                                         + (pager.visible ? pager.implicitWidth + 16 : 0)
                                         + 16
    readonly property bool wrapSelect: showSelect && width > 0 && topRowNeeded > width

    Layout.minimumHeight: 48
    spacing: 0

    component PageCheckBox: CheckBox {
        id: check
        // A page is only worth naming when there are several.
        text: root.episodes.pageCount > 1 ? "Seleccionar esta página" : "Seleccionar todo"
        tristate: true
        checkState: root.episodes.pageSelection
        // A click selects the whole page unless all of it already is.
        nextCheckState: () => root.episodes.pageSelection === Qt.Checked ? Qt.Unchecked
                                                                          : Qt.Checked
        Accessible.onPressAction: click()
        Keys.onReturnPressed: event => { event.accepted = true; click() }
        Keys.onEnterPressed: event => { event.accepted = true; click() }
        onClicked: {
            root.episodes.selectPage(checkState === Qt.Checked)
            checkState = Qt.binding(() => root.episodes.pageSelection)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 48
        Layout.minimumHeight: 48
        spacing: 8

        Label {
            id: headingLabel
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: 0
            text: "Episodios"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }

        Label {
            id: countLabel
            Layout.alignment: Qt.AlignVCenter
            text: "(" + root.episodes.total + ")"
            color: Theme.muted
            font.pixelSize: Theme.subtitle
        }

        BusyIndicator {
            id: busyIndicator
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            running: root.view.refreshing
            visible: running
        }

        PageCheckBox {
            id: inlineSelect
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 12
            visible: root.showSelect && !root.wrapSelect
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        Pager {
            id: pager
            Layout.alignment: Qt.AlignVCenter
            visible: root.episodes.pageCount > 1
            episodes: root.episodes
            onMoved: root.moved()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        Layout.bottomMargin: 4
        visible: root.showSelect && root.wrapSelect
        spacing: 8

        PageCheckBox {
            id: wrappedSelect
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
    }
}
