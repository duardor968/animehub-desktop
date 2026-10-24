import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// What airs next, as a week's agenda: a column per day with its anime, the hour and the
// episode. Not another row of posters: the home already has two.
Column {
    id: root

    // LibraryModel.upcomingDays: [{day, items: [{slug, title, posterUrl, caption}]}]
    property var days: []
    property int count: 0

    signal activated(string slug)

    readonly property int columnWidth: 220

    spacing: 12
    visible: days.length > 0

    RowLayout {
        width: root.width
        spacing: 8

        Label {
            text: "Próximos episodios"
            color: Theme.ink
            font.pixelSize: Theme.subtitle
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }

        Label {
            text: "(" + root.count + ")"
            color: Theme.muted
            font.pixelSize: Theme.subtitle
        }

        Item { Layout.fillWidth: true }
    }

    Flow {
        width: root.width
        spacing: Theme.posterGap

        Repeater {
            model: root.days

            delegate: Column {
                id: column

                required property var modelData

                width: root.columnWidth
                spacing: 4

                Label {
                    width: parent.width
                    bottomPadding: 4
                    text: column.modelData.day
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                }

                Repeater {
                    model: column.modelData.items

                    delegate: ItemDelegate {
                        id: entry

                        required property var modelData

                        width: column.width
                        height: 60
                        leftPadding: 4
                        rightPadding: 4
                        onClicked: root.activated(modelData.slug)
                        Accessible.name: modelData.title + ", " + column.modelData.day + " "
                                         + modelData.caption
                        ToolTip.visible: hovered && name.truncated
                        ToolTip.delay: 500
                        ToolTip.text: modelData.title

                        contentItem: RowLayout {
                            spacing: 10

                            Poster {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 51
                                source: entry.modelData.posterUrl
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    id: name
                                    Layout.fillWidth: true
                                    text: entry.modelData.title
                                    color: Theme.ink
                                    font.pixelSize: Theme.body
                                    elide: Text.ElideMiddle
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: entry.modelData.caption
                                    color: Theme.muted
                                    font.pixelSize: Theme.caption
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
