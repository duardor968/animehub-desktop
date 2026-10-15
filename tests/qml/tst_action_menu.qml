import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/components"

// ActionMenu lists only the entries that apply, each time it opens; every entry it shows must
// stay drawn while the pointer moves over it, also when an entry comes and goes between opens.
Item {
    id: root

    property bool secondWanted: false

    width: 600
    height: 400

    Button {
        id: anchor
        x: 20
        y: 20
        text: "…"
    }

    ActionMenu {
        id: menu

        entries: [first, second, separator, third, fourth]

        MenuItem {
            id: first
            hoverEnabled: true
            text: "Marcar todo como visto"
        }
        MenuItem {
            id: second
            property bool wanted: root.secondWanted
            hoverEnabled: true
            text: "Marcar todo como no visto"
        }
        MenuSeparator {
            id: separator
            property bool separator: true
        }
        MenuItem {
            id: third
            hoverEnabled: true
            text: "Cancelar la descarga"
        }
        MenuItem {
            id: fourth
            hoverEnabled: true
            text: "Abrir la carpeta"
        }
    }

    TestCase {
        name: "ActionMenu"
        when: windowShown

        // Pixels of an entry's row that differ from the row's own background: its text.
        function drawn(image, item) {
            const at = item.mapToItem(root, 0, item.height / 2)
            const background = image.pixel(at.x + 4, at.y)
            let marks = 0
            for (let x = at.x + 16; x < at.x + item.width - 8; x += 2) {
                const pixel = image.pixel(x, at.y)
                if (Math.abs(pixel.r - background.r) + Math.abs(pixel.g - background.g)
                        + Math.abs(pixel.b - background.b) > 0.3)
                    ++marks
            }
            return marks
        }

        function test_entriesStayDrawnUnderThePointer() {
            for (let round = 0; round < 3; ++round) {
                root.secondWanted = round % 2 === 1
                menu.show(anchor)
                tryVerify(() => menu.opened, 2000)
                wait(400) // the opening transition
                compare(menu.count, root.secondWanted ? 5 : 4)
                for (let i = 0; i < menu.count; ++i) {
                    const item = menu.itemAt(i)
                    mouseMove(item, item.width / 2, item.height / 2)
                    wait(100)
                }
                mouseMove(root, root.width - 4, root.height - 4)
                wait(400) // the hover fades out
                const image = grabImage(root)
                for (let i = 0; i < menu.count; ++i) {
                    const item = menu.itemAt(i)
                    if (item.separator !== true)
                        verify(drawn(image, item) > 0, `round ${round}: "${item.text}" is not drawn`)
                }
                menu.close()
                tryVerify(() => !menu.visible, 2000)
            }
        }
    }
}
