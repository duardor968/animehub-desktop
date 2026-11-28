import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import AnimeHub

// Compact hero header (~255–270 px) for an anime or franchise page: crisp backdrop artwork when
// available, or GPU MultiEffect poster blur (fading cleanly into Theme.canvas, with AmbientBackdrop
// fallback while decoding or missing), full breadcrumb on the top edge, selectable title and
// alternative title with a hover copy button, semantic status badge, inline AnimeAV1 score, and
// clickable genre chips.
Item {
    id: root

    property string title
    // Shown only when it differs from the title; the page decides that.
    property string subtitle
    // "9,1 (48 mil)" fallback when scoreText is not supplied.
    property string score
    // "7,4": Spanish-formatted score alone; paired with scoreSource ("AnimeAV1").
    property string scoreText: ""
    property string scoreSource: ""
    // "TV · 2023 · Finalizado · 28 episodios" (used as one-line fallback by FranchisePage).
    property string facts
    // Semantic status badge ("En emisión", "Próximamente", "En pausa", "Finalizado") and tone.
    property string statusText
    property string statusTone
    // Plain metadata without status ("TV · 2026 · 8 episodios").
    property string metaLine
    // "Acción, Comedia": plain fallback when genreChips is empty.
    property string genres
    // Interactive genre chips: [{name, slug}].
    property var genreChips: []
    property url posterUrl
    property url backdropUrl
    // Surface colour the bottom edge fades seamlessly into.
    property color surfaceColor: Theme.canvas
    // The places above the page, for the breadcrumb over the title: [{text, go}].
    property var steps: []
    // Where Tab goes from the title: the page's ActionBar.
    property Item bar: null
    readonly property Item titleItem: titleControl
    // The art spans the page; the poster and titles keep to the page's column.
    property real contentWidth: width - 2 * Theme.section

    signal genreSelected(string slug)

    property bool copiedJustNow: false

    readonly property bool compact: width < 680
    readonly property bool hasStructuredMeta: statusText !== "" || metaLine !== ""
    readonly property bool hasBackdrop: backdropUrl.toString() !== ""
                                        && backdropImg.status !== Image.Error
    readonly property bool hasBlurPoster: !hasBackdrop && posterUrl.toString() !== ""
                                          && blurPosterSrc.status === Image.Ready
                                          && width > 0 && height > 0
    readonly property string inlineScore: scoreText !== ""
                                          ? (scoreSource !== "" ? scoreText + " · " + scoreSource : scoreText)
                                          : score
    readonly property bool hasGenreChips: genreChips && genreChips.length > 0

    // Compact hero proportions (~262 px on desktop, ~236 px on compact panes) so episodes start
    // cleanly above the fold with zero empty top void.
    readonly property int titleSize: compact ? 22 : Theme.title
    readonly property int smallerTitle: compact ? Theme.heading : 24
    readonly property int posterWidth: compact ? 112 : 138
    readonly property int posterHeight: compact ? 160 : 196
    readonly property int topBand: steps.length > 0 ? 48 : 24
    // Width left for the title beside the poster, reserving room for the inline copy ToolButton.
    readonly property real titleWidth: Math.max(0, contentWidth - posterWidth - block.spacing - 40)

    // A page that opens takes the keyboard on its title, as a screen reader expects. The frame
    // shows only while the keyboard is in use, as Fluent's controls show none for a click.
    function focusTitle(reason) {
        titleControl.forceActiveFocus(reason !== undefined ? reason : InputModality.reason)
    }

    function copyTitle() {
        if (title === "")
            return
        const win = root.ApplicationWindow.window
        if (win && win.app) {
            win.app.copyToClipboard(title)
            win.app.message("Título copiado", false)
        }
        copiedJustNow = true
        copiedTimer.restart()
    }

    // Never shorter than the compact hero band, and grows smoothly if a very long title or many
    // genre chips wrap past the poster height so nothing collides with the breadcrumb.
    implicitHeight: Math.max(compact ? 236 : 262, block.implicitHeight + topBand + 20)

    Timer {
        id: copiedTimer
        interval: 1600
        onTriggered: root.copiedJustNow = false
    }

    // The title's lines as Text would break them at `metrics`' size.
    function lines(words, metrics, width) {
        const result = []
        let line = ""
        for (const word of words) {
            const candidate = line === "" ? word : line + " " + word
            if (line !== "" && metrics.advanceWidth(candidate) > width) {
                result.push(line)
                line = word
            } else {
                line = candidate
            }
        }
        if (line !== "")
            result.push(line)
        return result
    }

    // Picks the large title size when it fits cleanly in 1–2 lines, else steps down one size
    // without inserting artificial newlines so selecting & copying yields the exact title string.
    function fitTitleSize(title, width, largeSize, smallSize) {
        const words = title.split(/\s+/).filter(word => word !== "")
        if (width <= 0 || words.length < 2)
            return largeSize
        const broken = lines(words, largeTitle, width)
        if (broken.length === 1 || (broken.length === 2 && broken[1].indexOf(" ") >= 0))
            return largeSize
        return smallSize
    }

    // System font probe so selectable TextEdits match Fluent's exact platform UI font.
    Label {
        id: fontProbe
        visible: false
    }

    FontMetrics {
        id: largeTitle
        font.family: fontProbe.font.family
        font.pixelSize: root.titleSize
        font.weight: Font.DemiBold
    }

    // Reactive exact-engine width measurement matching titleEdit's active font (pixelSize/weight/family)
    // so single-line titles never stay stuck at compact (22 px) width when expanding to 28 px.
    Text {
        id: titleMeasure
        visible: false
        text: root.title
        font: titleEdit.font
    }

    TextMetrics {
        id: metaMetrics
        font: metaLabel.font
        text: root.metaLine
    }

    // Clipped background container: real backdrop, GPU MultiEffect poster blur, or AmbientBackdrop.
    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            anchors.fill: parent
            color: root.surfaceColor
        }

        // Fallback ambient glow while images decode or when both backdrop and poster are absent.
        AmbientBackdrop {
            anchors.fill: parent
            visible: (!root.hasBackdrop || backdropImg.status !== Image.Ready) && !root.hasBlurPoster
            backdropUrl: ""
            posterUrl: root.posterUrl
            surfaceColor: root.surfaceColor
            leftScrim: false
        }

        // Crisp backdrop artwork with smooth horizontal & bottom readability scrims.
        Item {
            anchors.fill: parent
            visible: root.hasBackdrop

            Image {
                id: backdropImg
                anchors.fill: parent
                source: root.backdropUrl.toString() !== "" ? root.backdropUrl : ""
                fillMode: Image.PreserveAspectCrop
                verticalAlignment: Image.AlignTop
                asynchronous: true
                cache: true
                sourceSize.width: Math.min(1920, Math.max(640, Math.ceil(width * Screen.devicePixelRatio)))
                opacity: status === Image.Ready ? 0.78 : 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.84) }
                    GradientStop { position: 0.45; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.50) }
                    GradientStop { position: 0.82; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.14) }
                    GradientStop { position: 1.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.04) }
                }
            }

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.12) }
                    GradientStop { position: 0.58; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.52) }
                    GradientStop { position: 0.90; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.94) }
                    GradientStop { position: 1.0; color: root.surfaceColor }
                }
            }
        }

        // Winning Hero Background A: GPU MultiEffect cinematic poster blur when backdrop is absent.
        Image {
            id: blurPosterSrc
            anchors.fill: parent
            source: !root.hasBackdrop ? root.posterUrl : ""
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            cache: true
            sourceSize.width: 480
            visible: false
        }

        MultiEffect {
            anchors.fill: parent
            source: blurPosterSrc
            visible: root.hasBlurPoster
            blurEnabled: true
            blur: 1.0
            blurMax: 64
            saturation: 0.15
            autoPaddingEnabled: false
            opacity: root.hasBlurPoster ? 0.36 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
        }

        Rectangle {
            anchors.fill: parent
            visible: root.hasBlurPoster
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.62) }
                GradientStop { position: 0.52; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.28) }
                GradientStop { position: 1.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.44) }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: root.hasBlurPoster
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.08) }
                GradientStop { position: 0.55; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.42) }
                GradientStop { position: 0.88; color: Qt.rgba(root.surfaceColor.r, root.surfaceColor.g, root.surfaceColor.b, 0.92) }
                GradientStop { position: 1.0; color: root.surfaceColor }
            }
        }
    }

    Breadcrumb {
        x: Theme.section
        y: 14
        width: root.contentWidth
        visible: root.steps.length > 0
        steps: root.steps
        current: root.title
    }

    RowLayout {
        id: block
        x: Theme.section
        width: root.contentWidth
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        spacing: root.compact ? 18 : 24

        Poster {
            Layout.preferredWidth: root.posterWidth
            Layout.preferredHeight: root.posterHeight
            Layout.alignment: Qt.AlignBottom
            source: root.posterUrl
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignBottom
            spacing: 6

            RowLayout {
                id: titleRow
                Layout.fillWidth: true
                spacing: 6

                HoverHandler { id: titleHover }

                // A Control, not a bare TextEdit, so a screen reader reads it as the page's heading
                // and keyboard focus lands cleanly on `pageTitle`. Its content is genuinely
                // mouse-selectable without injecting newlines.
                Control {
                    id: titleControl
                    objectName: "pageTitle"
                    Layout.preferredWidth: Math.min(Math.ceil(titleMeasure.implicitWidth) + 8,
                                                    Math.max(0, root.titleWidth))
                    Layout.maximumWidth: Math.max(0, root.titleWidth)
                    Layout.minimumWidth: 0
                    padding: 0
                    focusPolicy: Qt.TabFocus
                    Accessible.role: Accessible.Heading
                    Accessible.name: root.title
                    Keys.onTabPressed: event => event.accepted = root.bar !== null && root.bar.enter()

                    contentItem: TextEdit {
                        id: titleEdit
                        readOnly: true
                        selectByMouse: true
                        activeFocusOnTab: false
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentInk
                        text: root.title
                        color: Theme.ink
                        font.family: fontProbe.font.family
                        font.pixelSize: root.fitTitleSize(root.title, root.titleWidth,
                                                          root.titleSize, root.smallerTitle)
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        padding: 0
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 0
                        bottomPadding: 0
                        Accessible.ignored: true

                        HoverHandler { cursorShape: Qt.IBeamCursor }
                    }

                    // Around the words, not the column they sit in.
                    FocusFrame {
                        anchors.fill: undefined
                        x: -3
                        y: -3
                        width: Math.min(titleControl.width, Math.ceil(titleEdit.contentWidth)) + 6
                        height: Math.ceil(titleEdit.contentHeight) + 6
                        visible: titleControl.activeFocus && InputModality.keyboard
                        radiusInside: 2
                    }
                }

                // Subtle hover-only copy button tucked directly beside the title.
                ToolButton {
                    id: copyButton
                    visible: root.title !== ""
                    Layout.alignment: Qt.AlignVCenter
                    opacity: titleHover.hovered || copyButton.hovered
                             || (copyButton.activeFocus && InputModality.keyboard)
                             || root.copiedJustNow ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    icon.source: Theme.source(root.copiedJustNow ? Theme.icon.check : Theme.icon.copy)
                    icon.color: root.copiedJustNow ? Theme.success
                                : (copyButton.hovered ? Theme.ink : Theme.muted)
                    display: AbstractButton.IconOnly
                    text: "Copiar título"
                    Accessible.name: "Copiar título"
                    Accessible.onPressAction: click()
                    ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.timeout: Theme.tooltipTimeout
                    ToolTip.text: root.copiedJustNow ? "Título copiado" : "Copiar título"
                    Keys.onReturnPressed: event => { event.accepted = true; click() }
                    Keys.onEnterPressed: event => { event.accepted = true; click() }
                    onClicked: root.copyTitle()
                }

                Item { Layout.fillWidth: true }
            }

            TextEdit {
                Layout.fillWidth: true
                visible: root.subtitle !== ""
                readOnly: true
                selectByMouse: true
                activeFocusOnTab: false
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentInk
                text: root.subtitle
                color: Theme.muted
                font.family: fontProbe.font.family
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                HoverHandler { cursorShape: Qt.IBeamCursor }
            }

            // Franchise fallback (no statusText/metaLine): one clean wrapping line of facts.
            Label {
                readonly property string star: "<span style=\"font-family:'" + Theme.iconFont
                                                + "'; font-size:" + Theme.small + "px; color:" + Theme.warning + "\">"
                                                + Theme.glyph(Theme.icon.starFilled) + "</span>"
                readonly property string escapedScore: root.inlineScore
                                                       .replace(/&/g, "&amp;")
                                                       .replace(/</g, "&lt;")
                readonly property string scored: root.inlineScore === "" ? ""
                    : star + "&nbsp;<span style=\"color:" + Theme.ink + "; font-weight:600\">"
                      + escapedScore + "</span>"
                readonly property string described: [root.facts, root.genres].filter(part => part !== "")
                                                                             .join(" · ")

                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: !root.hasStructuredMeta && text !== ""
                textFormat: Text.RichText
                text: [scored, described.replace(/&/g, "&amp;").replace(/</g, "&lt;")]
                      .filter(part => part !== "").join(" · ")
                color: Theme.muted
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                Accessible.name: (root.inlineScore !== "" ? "Puntuación " + root.inlineScore + ". " : "") + described
            }

            // Anime metadata: one semantic status badge, inline AnimeAV1 score, and plain metadata.
            Flow {
                id: metaFlow
                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: root.hasStructuredMeta
                spacing: 8

                Rectangle {
                    id: statusBadge
                    readonly property color toneInk: root.statusTone === "airing" ? Theme.success
                                                   : root.statusTone === "upcoming" ? Theme.accentText
                                                   : root.statusTone === "paused" ? Theme.warning
                                                   : Theme.muted
                    readonly property color toneFill: root.statusTone === "airing"
                                                      ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.16)
                                                    : root.statusTone === "upcoming"
                                                      ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                                    : root.statusTone === "paused"
                                                      ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                                                    : Qt.rgba(1, 1, 1, 0.07)
                    readonly property color toneBorder: root.statusTone === "airing"
                                                        ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.30)
                                                      : root.statusTone === "upcoming"
                                                        ? Qt.rgba(Theme.accentText.r, Theme.accentText.g, Theme.accentText.b, 0.32)
                                                      : root.statusTone === "paused"
                                                        ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.30)
                                                      : Qt.rgba(1, 1, 1, 0.12)

                    visible: root.statusText !== ""
                    implicitWidth: statusLabel.implicitWidth + 16
                    implicitHeight: 22
                    height: 22
                    radius: Theme.radiusSm
                    color: toneFill
                    border.width: 1
                    border.color: toneBorder
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.statusText

                    Label {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: root.statusText
                        color: statusBadge.toneInk
                        font.pixelSize: Theme.caption
                        font.weight: Font.DemiBold
                        Accessible.ignored: true
                    }
                }

                Row {
                    id: scoreRow
                    visible: root.inlineScore !== ""
                    height: Math.max(22, implicitHeight)
                    spacing: 4
                    Accessible.role: Accessible.StaticText
                    Accessible.name: "Puntuación " + root.inlineScore

                    Glyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icon.starFilled
                        size: Theme.small
                        color: Theme.warning
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.scoreText !== "" ? root.scoreText : root.score
                        color: Theme.ink
                        font.pixelSize: Theme.body
                        font.weight: Font.DemiBold
                        Accessible.ignored: true
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.scoreText !== "" && root.scoreSource !== ""
                        text: "· " + root.scoreSource
                        color: Theme.muted
                        font.pixelSize: Theme.small
                        Accessible.ignored: true
                    }
                }

                Label {
                    visible: root.inlineScore !== "" && root.metaLine !== ""
                    opacity: metaLabel.y <= scoreRow.y + 4 ? 1 : 0
                    height: Math.max(22, implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: "·"
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    Accessible.ignored: true
                }

                Label {
                    id: metaLabel
                    visible: root.metaLine !== ""
                    width: Math.min(Math.ceil(metaMetrics.advanceWidth), metaFlow.width)
                    height: Math.max(22, implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: root.metaLine
                    color: Theme.muted
                    font.pixelSize: Theme.body
                    wrapMode: Text.Wrap
                }
            }

            // Clickable genre chips that navigate to Catálogo filtered by that genre.
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: root.hasStructuredMeta && root.hasGenreChips
                spacing: 6

                Repeater {
                    model: root.genreChips

                    delegate: AbstractButton {
                        id: chip
                        required property var modelData

                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: 24
                        implicitWidth: chipLabel.implicitWidth + 18
                        text: modelData.name ?? ""
                        Accessible.role: Accessible.Button
                        Accessible.name: "Filtrar catálogo por " + text
                        Accessible.onPressAction: clicked()
                        ToolTip.visible: hovered || (activeFocus && InputModality.keyboard)
                        ToolTip.delay: Theme.tooltipDelay
                        ToolTip.timeout: Theme.tooltipTimeout
                        ToolTip.text: "Ver animes de " + text + " en el catálogo"
                        onClicked: if (modelData.slug) root.genreSelected(modelData.slug)
                        Keys.onReturnPressed: event => { event.accepted = true; clicked() }
                        Keys.onEnterPressed: event => { event.accepted = true; clicked() }
                        Keys.onSpacePressed: event => { event.accepted = true; clicked() }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }

                        background: Rectangle {
                            radius: 12
                            color: chip.down
                                   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                                   : chip.hovered
                                     ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                                     : Qt.rgba(1, 1, 1, 0.08)
                            border.width: 1
                            border.color: chip.hovered
                                          ? Qt.rgba(Theme.accentText.r, Theme.accentText.g, Theme.accentText.b, 0.45)
                                          : Qt.rgba(1, 1, 1, 0.12)
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }

                        contentItem: Label {
                            id: chipLabel
                            text: chip.text
                            color: chip.hovered ? Theme.accentText : Theme.ink
                            opacity: chip.hovered ? 1.0 : 0.85
                            font.pixelSize: Theme.caption
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Accessible.ignored: true
                        }

                        FocusFrame {
                            visible: chip.activeFocus && InputModality.keyboard
                            radiusInside: 12
                        }
                    }
                }
            }

            // Plain genre text fallback when genreChips is not populated.
            Label {
                Layout.fillWidth: true
                visible: root.hasStructuredMeta && !root.hasGenreChips && root.genres !== ""
                text: root.genres
                color: Theme.muted
                font.pixelSize: Theme.body
                elide: Text.ElideRight
            }
        }
    }
}
