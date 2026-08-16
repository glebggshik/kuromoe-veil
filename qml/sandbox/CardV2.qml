import QtQuick

// Альтернативный дизайн для сравнения с CardV1 — постер и текст разделены
// (не текст поверх картинки), больше воздуха, рейтинг как акцентная плашка
// слева сверху. Правь свободно — это черновик для сравнения, не прод-код.
Rectangle {
    id: root
    width: 160
    height: 250
    radius: Theme.cornerSmall
    color: Theme.bgCard
    clip: true

    property var anime: ({})
    property bool imagesEnabled: true
    property bool showAiredEpisode: false
    readonly property string placeholderLetter: {
        var t = (anime.title || "?").trim()
        return t.length > 0 ? t.charAt(0).toUpperCase() : "?"
    }
    readonly property string scoreText: {
        var n = Number(anime.score)
        return (isNaN(n) || n <= 0) ? "" : n.toFixed(1)
    }
    signal clicked()

    Column {
        anchors.fill: parent
        spacing: 0

        PosterMock {
            width: parent.width
            height: 160
            posterActive: root.imagesEnabled
            cornerRadius: 0
            placeholderLetter: root.placeholderLetter
        }

        Column {
            width: parent.width
            padding: 8
            spacing: 3

            Text {
                width: parent.width - 16
                text: anime.title || "?"
                color: Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Row {
                spacing: 6
                Text {
                    visible: (anime.year || 0) > 0
                    text: anime.year || ""
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Text {
                    visible: (anime.episodes || 0) > 0
                    text: (anime.episodes || 0) + " эп."
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }
        }
    }

    Rectangle {
        visible: (anime.score || 0) > 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 8
        radius: Theme.cornerPill
        color: Theme.accent
        width: scoreText2.width + 16
        height: 22
        Text {
            id: scoreText2
            anchors.centerIn: parent
            text: root.scoreText
            color: "white"
            font.pixelSize: 11
            font.bold: true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: hoverArea.containsMouse ? 2 : 0
        border.color: Theme.accentLight
        Behavior on border.width { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
