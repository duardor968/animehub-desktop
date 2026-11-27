import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import AnimeHub

// Full-width atmospheric Hero spotlight at the top of LibraryHome: dominant-color ambient glow
// (or real backdrop) behind a crisp poster and clean typography, with primary play and secondary
// details actions side by side — never overlaying buttons on artwork.
Item {
    id: root

    // LibraryModel.featured
    required property var featured
    property bool compact: width > 0 && width < 560
    property color canvasColor: Theme.canvas

    signal open(string slug)
    signal act(string slug)
    // → from the card: on to the posters below it.
    signal next()

    readonly property int pad: compact ? 16 : 22
    readonly property bool hasBackdrop: (featured && featured.backdropUrl ? featured.backdropUrl.toString() : "") !== ""

    readonly property string kickerPrimary: (featured && featured.actionText === "Empezar")
                                            ? "Empezar a ver" : "Continuar viendo"

    readonly property string kickerMeta: {
        const f = featured ?? ({})
        const parts = []
        if (f.statusText)
            parts.push(f.statusText)
        if ((f.unwatchedOnDisk ?? 0) > 1)
            parts.push(f.unwatchedOnDisk + " episodios en disco")
        return parts.join(" · ")
    }

    readonly property string subtitleText: {
        const f = featured ?? ({})
        const ep = f.episodeLabel ?? ""
        const rem = f.remainingText ?? ""
        const det = f.detail ?? ""
        if (ep !== "" && rem !== "")
            return ep + " · " + rem
        if (det !== "") {
            const sep = det.indexOf(" · ")
            if (ep !== "" && sep >= 0)
                return ep + det.slice(sep)
            return ep !== "" ? ep : det
        }
        return ep
    }

    readonly property string primaryActionText: {
        const f = featured ?? ({})
        const base = f.actionText || "Continuar"
        const ep = (f.episodeLabel ?? "").replace(/^Episodio\s+/, "ep. ")
        return ep !== "" ? base + " " + ep : base
    }

    // The keyboard arrives here from the shelf below (← from its first poster): focus the title
    // control whose `.text` matches `featured.title`.
    function forceFocus(reason) { heroTitleBtn.forceActiveFocus(reason) }

    implicitHeight: Math.max(compact ? 186 : 216, layout.implicitHeight + 2 * pad)

    // Clipped rounded card backdrop so focus rings in the foreground are never cut off.
    Item {
        anchors.fill: parent
        clip: true

        // Real backdrop when available.
        AmbientBackdrop {
            anchors.fill: parent
            anchors.margins: 1
            visible: root.hasBackdrop
            backdropUrl: root.featured ? (root.featured.backdropUrl ?? "") : ""
            posterUrl: root.featured ? (root.featured.posterUrl ?? "") : ""
            surfaceColor: Theme.pane
            leftScrim: true
            glowStrength: 1.15
        }

        // Extra readability scrim over real backdrops so eyebrow, title and secondary button keep
        // crisp AA contrast even against bright upper-center artwork highlights at narrow widths.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            visible: root.hasBackdrop
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.72) }
                GradientStop { position: 0.58; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.56) }
                GradientStop { position: 0.85; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.24) }
                GradientStop { position: 1.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.06) }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            visible: root.hasBackdrop
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.42) }
                GradientStop { position: 0.45; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.0) }
            }
        }

        // Approved Hero Bg A fallback when backdropUrl is absent: GPU MultiEffect subtle poster blur.
        Item {
            anchors.fill: parent
            anchors.margins: 1
            visible: !root.hasBackdrop

            Rectangle {
                anchors.fill: parent
                color: Theme.pane
            }

            Image {
                id: blurPosterSrc
                anchors.fill: parent
                source: (!root.hasBackdrop && root.featured) ? (root.featured.posterUrl ?? "") : ""
                fillMode: Image.PreserveAspectCrop
                verticalAlignment: Image.AlignVCenter
                asynchronous: true
                cache: true
                sourceSize.width: 320
                visible: false
            }

            MultiEffect {
                anchors.fill: parent
                visible: blurPosterSrc.status === Image.Ready && width > 0 && height > 0
                source: blurPosterSrc
                blurEnabled: true
                blur: 1.0
                blurMax: 64
                saturation: 0.15
                autoPaddingEnabled: false
                opacity: blurPosterSrc.status === Image.Ready ? 0.40 : 0.0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }

            // Soft horizontal readability scrim.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.62) }
                    GradientStop { position: 0.52; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.28) }
                    GradientStop { position: 1.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.44) }
                }
            }

            // Soft vertical grounding scrim into Theme.pane.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.08) }
                    GradientStop { position: 0.55; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.42) }
                    GradientStop { position: 0.88; color: Qt.rgba(Theme.pane.r, Theme.pane.g, Theme.pane.b, 0.92) }
                    GradientStop { position: 1.0; color: Theme.pane }
                }
            }
        }

        // Anti-aliased inner radius-8 corner trim against the surrounding canvas.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: 12
            color: "transparent"
            border.width: 4
            border.color: root.canvasColor
        }

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "transparent"
            border.width: 1
            border.color: Theme.stroke
        }
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        spacing: root.pad

        // Crisp poster beside the copy, completely clean of overlaid controls.
        Poster {
            id: heroPoster
            Layout.preferredWidth: root.compact ? 102 : 122
            Layout.preferredHeight: root.compact ? 144 : 172
            Layout.alignment: Qt.AlignVCenter
            source: root.featured.posterUrl ?? ""
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            TapHandler { onTapped: root.open(root.featured.slug) }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: root.kickerPrimary
                    color: Theme.accentText
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: root.kickerMeta !== ""
                    text: "· " + root.kickerMeta
                    color: Theme.muted
                    font.pixelSize: Theme.caption
                    elide: Text.ElideRight
                }
            }

            AbstractButton {
                id: heroTitleBtn

                Layout.fillWidth: true
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                text: root.featured.title ?? ""
                background: null
                onClicked: root.open(root.featured.slug)
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onRightPressed: root.next()
                Accessible.name: text + (root.subtitleText !== "" ? ", " + root.subtitleText : "")
                ToolTip.visible: hovered && titleLabel.truncated
                ToolTip.delay: 500
                ToolTip.text: text

                contentItem: Label {
                    id: titleLabel
                    text: heroTitleBtn.text
                    color: heroTitleBtn.hovered ? "#ffffff" : Theme.ink
                    font.pixelSize: root.compact ? 20 : 23
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignTop
                    Accessible.ignored: true
                }
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.subtitleText
                color: Theme.muted
                font.pixelSize: Theme.body
                elide: Text.ElideRight
            }

            ProgressLine {
                Layout.fillWidth: true
                Layout.maximumWidth: 320
                Layout.topMargin: 2
                visible: (root.featured.progress ?? -1) >= 0
                value: root.featured.progress ?? 0
            }

            RowLayout {
                Layout.topMargin: 6
                spacing: 8

                AccentButton {
                    id: playButton
                    text: root.primaryActionText
                    glyphCode: Theme.icon.play
                    iconSize: 13
                    onClicked: root.act(root.featured.slug)
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    Keys.onRightPressed: root.next()
                    Accessible.name: text + ", " + (root.featured.title ?? "")
                }

                Button {
                    id: detailsButton
                    text: "Ver ficha"
                    onClicked: root.open(root.featured.slug)
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    Keys.onRightPressed: root.next()
                    Accessible.name: "Ver ficha de " + (root.featured.title ?? "")
                }
            }
        }
    }

    // Card-level keyboard focus frame matching PosterCard, never boxing only the headline text.
    FocusFrame {
        anchors.fill: parent
        radiusInside: 8
        visible: heroTitleBtn.activeFocus && InputModality.keyboard
    }
}
