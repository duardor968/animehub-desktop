import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeHub

// The top of an anime's or a franchise's page, as Steam's hero: a band of the backdrop fading
// into the canvas, and on its foot the poster with the title and one line of facts. Low, so the
// page's content starts on the first screen. Without a backdrop, the poster alone: no empty band.
Item {
    id: root

    property string title
    // Shown only when it differs from the title; the page decides that.
    property string subtitle
    // "9,1 (48 mil)", put before the facts with a star.
    property string score
    // "TV · 2023 · Finalizado · 28 episodios"
    property string facts
    // "Acción, Comedia", closing the line of facts.
    property string genres
    property url posterUrl
    property url backdropUrl
    // The art spans the page; the poster and titles keep to the page's column.
    property real contentWidth: width - 2 * Theme.section

    // Space is kept while the backdrop loads, so the page does not jump when it arrives.
    readonly property bool hasBackdrop: backdropUrl.toString() !== "" && backdrop.status !== Image.Error
    // One step down when the title would need a third line or leave a word alone on its second.
    readonly property int smallerTitle: 24
    // The titles' column: what the poster leaves, not the label's own width, which its size moves.
    readonly property real titleWidth: contentWidth - Theme.posterWidth - block.spacing

    // About 4:1 (300 px on a 1300 px page), never above 340 nor below the poster and its margins.
    implicitHeight: hasBackdrop ? Math.min(340, Math.max(Math.round(width / 4.3), block.implicitHeight + 44))
                                : block.implicitHeight + 28 + 24

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

    // At most two lines, never one word alone on the second: the full size if it allows that,
    // else one step down, and there the last word of the first line joins the lone one.
    function fitTitle(title, width) {
        const words = title.split(/\s+/).filter(word => word !== "")
        if (width <= 0 || words.length < 2)
            return { text: title, size: Theme.title }
        for (const metrics of [largeTitle, smallTitle]) {
            const broken = lines(words, metrics, width)
            if (broken.length === 1 || (broken.length === 2 && broken[1].indexOf(" ") >= 0))
                return { text: title, size: metrics.font.pixelSize }
        }
        const broken = lines(words, smallTitle, width)
        if (broken.length === 2) {
            const first = broken[0].split(" ")
            if (first.length > 1) {
                const second = first.pop() + " " + broken[1]
                if (smallTitle.advanceWidth(second) <= width)
                    return { text: first.join(" ") + "\n" + second, size: root.smallerTitle }
            }
        }
        return { text: title, size: root.smallerTitle }
    }

    FontMetrics {
        id: largeTitle
        font.family: titleLabel.font.family
        font.pixelSize: Theme.title
        font.weight: Font.DemiBold
    }

    FontMetrics {
        id: smallTitle
        font.family: titleLabel.font.family
        font.pixelSize: root.smallerTitle
        font.weight: Font.DemiBold
    }

    Image {
        id: backdrop
        anchors.fill: parent
        visible: root.hasBackdrop
        source: root.backdropUrl
        fillMode: Image.PreserveAspectCrop
        // Cropped, the art keeps its top, where the faces usually are.
        verticalAlignment: Image.AlignTop
        asynchronous: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        opacity: status === Image.Ready ? 0.8 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // A sibling, not a child: it must not inherit the image's opacity to reach the canvas.
    Rectangle {
        anchors.fill: backdrop
        visible: root.hasBackdrop
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.1) }
            GradientStop { position: 0.5; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.45) }
            GradientStop { position: 1.0; color: Theme.pane }
        }
    }

    RowLayout {
        id: block
        x: Theme.section
        width: root.contentWidth
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 24

        Poster {
            Layout.preferredWidth: Theme.posterWidth
            Layout.preferredHeight: Math.round(Theme.posterWidth * 1.42)
            Layout.alignment: Qt.AlignBottom
            source: root.posterUrl
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignBottom
            spacing: 6

            Label {
                id: titleLabel
                readonly property var fit: root.fitTitle(root.title, root.titleWidth)
                Layout.fillWidth: true
                text: fit.text
                color: Theme.ink
                font.pixelSize: fit.size
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: root.title
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.subtitle
                color: Theme.muted
                font.pixelSize: Theme.body
                elide: Text.ElideRight
            }

            // "★ 9,1 (48 mil) · TV · 2023 · Finalizado · 28 episodios · Acción, Comedia": one
            // text, so a narrow column wraps it between words instead of cutting it.
            Label {
                readonly property string star: "<span style=\"font-family:'" + Theme.iconFont
                                                + "'; font-size:13px\">" + Theme.glyph(Theme.icon.starFilled)
                                                + "</span>"
                readonly property string scored: root.score === "" ? ""
                    : "<span style=\"color:" + Theme.ink + "; font-weight:600\">" + star + "&nbsp;"
                      + root.score + "</span>"
                readonly property string described: [root.facts, root.genres].filter(part => part !== "")
                                                                             .join(" · ")

                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: text !== ""
                textFormat: Text.RichText
                text: [scored, described.replace(/&/g, "&amp;").replace(/</g, "&lt;")]
                      .filter(part => part !== "").join(" · ")
                color: Theme.muted
                font.pixelSize: Theme.body
                wrapMode: Text.Wrap
                Accessible.name: (root.score !== "" ? "Puntuación " + root.score + ". " : "") + described
            }
        }
    }
}
