import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Steam-style library: the list on the left; on the right, the home with posters when nothing
// is selected, or the selected anime.
Item {
    id: page

    required property AppController app
    property string selected

    signal browseCatalog()

    function select(slug) {
        selected = slug
        if (slug)
            app.openLibraryAnime(slug)
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // A hairline that is easy to grab.
        handle: Item {
            implicitWidth: 6

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: parent.height
                color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.stroke
            }
        }

        LibraryList {
            SplitView.preferredWidth: page.app.settings.value("library/listWidth", 300)
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 480
            model: page.app.library
            selectedSlug: page.selected
            onActivated: slug => page.select(slug)
            onWidthChanged: if (width > 0) page.app.settings.setValue("library/listWidth", Math.round(width))
        }

        StackLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 480
            // Not the widest child's implicit width (an unwrapped synopsis): the split decides.
            implicitWidth: 480
            currentIndex: page.selected ? 1 : 0

            LibraryHome {
                library: page.app.library
                onActivated: slug => page.select(slug)
                onBrowseCatalog: page.browseCatalog()
            }

            AnimePage {
                view: page.app.libraryAnime
                backText: "Biblioteca"
                onBack: page.select("")
            }
        }
    }
}
