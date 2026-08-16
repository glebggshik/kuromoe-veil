import QtQuick
import AnimeClient 1.0
import QtQuick.Effects

// Порт components/anime-card.tsx: обложка 2:3, рейтинг-бейдж (янтарь) слева
// сверху, статус-бейдж справа сверху, play-иконка по центру на hover,
// название + год/эп под обложкой. Острые углы — никакого radius нигде.
// Данные — те же поля, что и в обычном Card.qml (посер/жанры реальные,
// приходят от CatalogBridge/DetailBridge/BookmarksBridge, не мок).
Rectangle {
    id: root
    width: 168
    height: 168 * 1.5 + 46  // aspect 2:3 обложка + текстовый блок снизу
    color: RetroTheme.card
    border.width: 1
    border.color: hoverArea.containsMouse ? RetroTheme.primary : RetroTheme.border
    Behavior on border.color { ColorAnimation { duration: 150 } }

    property var anime: ({})
    property bool imagesEnabled: true
    signal clicked()

    readonly property real coverHeight: width * 1.5
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
        return (isNaN(n) || n <= 0) ? "" : n.toFixed(1)
    }
    // Shikimori отдаёт status нижним регистром ("ongoing"/"released"/"anons") — приводим к макету.
    readonly property string statusText: (anime.status || "").toString().toUpperCase()
    readonly property bool statusIsOngoing: (anime.status || "") === "ongoing"

    Item {
        id: coverArea
        width: parent.width
        height: root.coverHeight
        clip: true

        PosterThumbnail {
            anchors.fill: parent
            posterSource: root.posterSource
            posterActive: root.imagesEnabled
            cornerRadius: 0
            placeholderLetter: root.placeholderLetter
        }

        // Затемнение снизу (аналог bg-gradient-to-t from-background/90)
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.9) }
            }
        }

        // Рейтинг — левый верхний, янтарная рамка
        Rectangle {
            visible: root.scoreText.length > 0
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 4
            width: ratingRow.width + 10
            height: 18
            color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
            border.width: 1
            border.color: Qt.rgba(1, 0.702, 0.169, 0.7)

            Row {
                id: ratingRow
                anchors.centerIn: parent
                spacing: 3
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "★"
                    color: RetroTheme.accent
                    font.pixelSize: 9
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.scoreText
                    color: RetroTheme.accent
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                }
            }
        }

        // Статус — правый верхний
        Rectangle {
            visible: root.statusText.length > 0
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            width: statusText.width + 10
            height: 18
            color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
            border.width: 1
            border.color: root.statusIsOngoing
                ? Qt.rgba(0.220, 0.973, 0.573, 0.7)
                : Qt.rgba(0.435, 0.506, 0.447, 0.5)
            Text {
                id: statusText
                anchors.centerIn: parent
                text: root.statusText
                color: root.statusIsOngoing ? RetroTheme.primary : RetroTheme.mutedForeground
                font.family: RetroTheme.fontFamily
                font.pixelSize: 8
            }
        }

        // Play-иконка по центру — только на hover
        Rectangle {
            anchors.centerIn: parent
            width: 40; height: 40
            color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
            border.width: 1
            border.color: RetroTheme.primary
            visible: opacity > 0
            opacity: hoverArea.containsMouse ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: RetroTheme.primary
                shadowBlur: 0.6
                shadowOpacity: 0.5
            }

            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 1
                text: "▶"
                color: RetroTheme.primary
                font.pixelSize: 14
            }
        }
    }

    Rectangle {
        anchors.top: coverArea.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: RetroTheme.border
    }

    Column {
        anchors.top: coverArea.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        anchors.topMargin: 9
        spacing: 3

        Text {
            width: parent.width
            text: anime.title || "?"
            color: hoverArea.containsMouse ? RetroTheme.primary : RetroTheme.foreground
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            elide: Text.ElideRight
            Behavior on color { ColorAnimation { duration: 150 } }
        }
        Text {
            width: parent.width
            text: (anime.year ? anime.year : "") + (anime.episodes ? "  ·  EP " + anime.episodes : "")
            color: RetroTheme.mutedForeground
            font.family: RetroTheme.fontFamily
            font.pixelSize: 9
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
