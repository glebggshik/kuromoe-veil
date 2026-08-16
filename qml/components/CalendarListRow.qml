import QtQuick
import AnimeClient 1.0

Rectangle {
    id: root
    height: 112
    radius: Theme.cornerSmall
    color: mouse.containsMouse ? Theme.bgCardHover : Theme.bgCard
    clip: true

    property var anime: ({})
    property bool posterEnabled: true
    signal clicked()

    function posterOk(u) {
        return u.length > 0 && !u.endsWith(".webp") && u.indexOf("/missing_") < 0
    }

    readonly property string posterSource: {
        var hd = anime.posterHd || ""
        var p = anime.poster || ""
        if (posterOk(hd)) return hd
        if (posterOk(p)) return p
        return hd || p
    }

    readonly property string placeholderLetter: {
        var t = (anime.title || "?").trim()
        return t.length > 0 ? t.charAt(0).toUpperCase() : "?"
    }

    Row {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        PosterThumbnail {
            width: 68
            height: 96
            posterSource: root.posterSource
            posterActive: root.posterEnabled
            placeholderLetter: root.placeholderLetter
        }

        Column {
            width: parent.width - 80
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Text {
                width: parent.width
                text: anime.title || "?"
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                text: "эп. " + (anime.nextEpisode || "?")
                    + ((anime.airTime || "").length > 0 ? "  ·  " + anime.airTime : "")
                color: Theme.accentLight
                font.pixelSize: 12
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}