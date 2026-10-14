import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The anime watched last, in large over its backdrop, with the button that goes on with it.
// Without a backdrop its poster stands beside the titles, as on its page.
Item {
    id: root

    // LibraryModel.featured
    required property var featured
    // Left margin of the text: the home's own.
    property int inset: Theme.section

    signal open(string slug)
    signal act(string slug)

    readonly property bool hasBackdrop: backdrop.status === Image.Ready
    readonly property bool downloading: featured.action === LibraryModel.Downloading

    implicitHeight: hasBackdrop ? Math.round(Math.min(380, Math.max(300, width * 0.32)))
                                : content.implicitHeight + 28

    Image {
        id: backdrop
        anchors.fill: parent
        source: root.featured.backdropUrl ?? ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        opacity: status !== Image.Ready ? 0 : hover.containsMouse ? 0.9 : 0.8
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // Siblings, not children: they must not inherit the image's opacity to reach the canvas.
    // The text side darkens; the bottom melts into the shelves.
    Rectangle {
        anchors.fill: parent
        visible: root.hasBackdrop
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.85) }
            GradientStop { position: 0.6; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0) }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: root.hasBackdrop
        gradient: Gradient {
            GradientStop { position: 0.35; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0) }
            GradientStop { position: 1.0; color: Theme.canvas }
        }
    }

    // A click anywhere but the button opens the anime.
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.open(root.featured.slug)
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.inset
        anchors.rightMargin: root.inset
        spacing: 24

        Poster {
            visible: !root.hasBackdrop
            Layout.preferredWidth: Theme.posterWidth
            Layout.preferredHeight: Math.round(Theme.posterWidth * 1.42)
            Layout.alignment: Qt.AlignBottom
            source: root.featured.posterUrl ?? ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 560
            Layout.alignment: Qt.AlignBottom
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: root.featured.title ?? ""
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
                text: root.featured.detail ?? ""
                color: Theme.muted
                font.pixelSize: Theme.body
                elide: Text.ElideRight
            }

            ProgressLine {
                Layout.preferredWidth: 280
                Layout.topMargin: 4
                visible: (root.featured.progress ?? -1) >= 0
                value: root.featured.progress ?? 0
            }

            Button {
                Layout.topMargin: 14
                text: root.featured.actionText ?? ""
                highlighted: !root.downloading
                enabled: !root.downloading
                icon.source: Theme.source(root.featured.action === LibraryModel.Play ? Theme.icon.play
                                                                                      : Theme.icon.download)
                onClicked: root.act(root.featured.slug)
                Accessible.name: text + ", " + (root.featured.title ?? "")
            }
        }
    }
}
