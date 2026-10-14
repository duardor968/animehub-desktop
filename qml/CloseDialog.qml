import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// Asked the first times the window closes: keep running in the tray, or quit.
Dialog {
    id: dialog

    required property AppController app

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent.width - 48)
    modal: true
    title: "¿Seguir en la bandeja?"

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 16

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.ink
            text: dialog.app.downloads.activeCount > 0
                  ? "Las descargas en curso y los avisos de episodios nuevos siguen mientras AnimeHub esté en la bandeja."
                  : "Los avisos de episodios nuevos siguen mientras AnimeHub esté en la bandeja."
        }

        CheckBox {
            id: remember
            text: "No volver a preguntar"
        }
    }

    footer: DialogButtonBox {
        Button {
            text: "Seguir en la bandeja"
            highlighted: true
            onClicked: {
                dialog.close()
                dialog.app.closeTo(AppSettings.Tray, remember.checked)
            }
        }
        Button {
            text: "Salir"
            onClicked: {
                dialog.close()
                dialog.app.closeTo(AppSettings.Quit, remember.checked)
            }
        }
    }
}
