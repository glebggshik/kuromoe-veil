import QtQuick

// Копия qml/components/Card.qml (текущий продовый дизайн) — только
// PosterThumbnail (C++) заменён на PosterMock (заглушка того же вида).
// Theme.xxx резолвится через singleton в этой же папке (см. qmldir).
Rectangle {
    id: root
    width: 160
    height: 230
    radius: Theme.corner
    color: Theme.bgCard
    clip: true

    property var anime: ({})
    property bool imagesEnabled: true
    property bool showAiredEpisode: false
    readonly property string posterSource: {
        var hd = anime.posterHd || ""
        var p = anime.poster || ""
        function ok(u) { return u.length > 0 && u.indexOf("/missing_") < 0 }
        if (ok(hd)) return hd
        if (ok(p)) return p
        return hd || p
    }
    readonly property string placeholderLetter: {
        var t = (anime.title || "?").trim()
        return t.length > 0 ? t.charAt(0).toUpperCase() : "?"
    }
    readonly property string scoreText: {
        var n = Number(anime.score)
        return (isNaN(n) || n <= 0) ? "" : n.toFixed(2)
    }
    readonly property string metaPillText: {
        var aired = Number(anime.episodesAired || 0)
        if (root.showAiredEpisode && aired > 0) {
            var n = Math.floor(aired)
            var mod10 = n % 10
            var mod100 = n % 100
            var word = "серий"
            if (mod100 < 11 || mod100 > 14) {
                if (mod10 === 1) word = "серия"
                else if (mod10 >= 2 && mod10 <= 4) word = "серии"
            }
            return n + " " + word
        }
        if ((anime.year || 0) > 0)
            return String(anime.year)
        return ""
    }
    readonly property bool metaPillIsYear: !root.showAiredEpisode || Number(anime.episodesAired || 0) <= 0
    signal clicked()
    signal genreClicked(int genreId)
    signal yearClicked(int year)

    PosterMock {
        anchors.fill: parent
        posterSource: root.posterSource
        posterActive: root.imagesEnabled
        cornerRadius: Theme.corner
        placeholderLetter: root.placeholderLetter
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.55
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: "#dd000000" }
        }
    }

    Rectangle {
        visible: (anime.score || 0) > 0
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        radius: 10
        color: "#a6000000"
        width: scoreRow.width + 14
        height: 22

        Row {
            id: scoreRow
            anchors.centerIn: parent
            spacing: 3
            Text { text: "★"; color: Theme.warn; font.pixelSize: 12 }
            Text { text: root.scoreText; color: "white"; font.pixelSize: 12; font.bold: true }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 2

        Text {
            width: parent.width
            text: anime.title || "?"
            color: "white"
            font.pixelSize: 12
            font.bold: true
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: (anime.kind ? anime.kind.toUpperCase() : "") + (anime.episodes ? "  ·  " + anime.episodes + " эп." : "")
            color: "#c8c8cd"
            font.pixelSize: 10
            visible: text.length > 0
        }

        Flow {
            width: parent.width
            topPadding: 3
            spacing: 4
            z: 2

            Repeater {
                model: (anime.genreTags || []).slice(0, 2)
                delegate: Rectangle {
                    radius: 8
                    color: "#33ffffff"
                    width: tagText.width + 10
                    height: 16
                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: modelData.name
                        color: "white"
                        font.pixelSize: 9
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.genreClicked(modelData.id)
                    }
                }
            }

            Rectangle {
                visible: root.metaPillText.length > 0
                radius: 8
                color: "#33ffffff"
                width: metaPillTextItem.width + 10
                height: 16
                Text {
                    id: metaPillTextItem
                    anchors.centerIn: parent
                    text: root.metaPillText
                    color: "white"
                    font.pixelSize: 9
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: root.metaPillIsYear ? Qt.PointingHandCursor : Qt.ArrowCursor
                    enabled: root.metaPillIsYear
                    onClicked: root.yearClicked(anime.year)
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: hoverArea.containsMouse ? 2 : 0
        border.color: Theme.accent
        Behavior on border.width { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        id: hoverArea
        z: 1
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    opacity: 0
    Component.onCompleted: appearAnim.start()
    NumberAnimation { id: appearAnim; target: root; property: "opacity"; to: 1; duration: 220; easing.type: Easing.OutCubic }
}
