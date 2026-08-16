import QtQuick
import QtQuick.Effects

// Порт components/anime-card.tsx: обложка 2:3, рейтинг-бейдж (янтарь) слева
// сверху, статус-бейдж справа сверху, play-иконка по центру на hover,
// название + год/эп под обложкой. Острые углы — никакого radius нигде.
Rectangle {
    id: root
    width: 168
    height: 168 * 1.5 + 46  // aspect 2:3 обложка + текстовый блок снизу
    color: RetroTheme.card
    border.width: 1
    border.color: hoverArea.containsMouse ? RetroTheme.primary : RetroTheme.border
    Behavior on border.color { ColorAnimation { duration: 150 } }

    property var anime: ({})
    signal clicked()

    readonly property real coverHeight: width * 1.5

    Item {
        id: coverArea
        width: parent.width
        height: root.coverHeight
        clip: true

        Image {
            anchors.fill: parent
            source: root.anime.cover || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
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
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 4
            width: ratingRow.width + 10
            height: 18
            color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
            border.width: 1
            border.color: Qt.rgba(1, 0.702, 0.169, 0.7)
            z: -1
        }
        Row {
            id: ratingRow
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 4
            anchors.leftMargin: 9
            anchors.topMargin: 4
            spacing: 3
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "★"
                color: RetroTheme.accent
                font.pixelSize: 9
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (root.anime.rating || 0).toFixed(1)
                color: RetroTheme.accent
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
            }
        }

        // Статус — правый верхний
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            width: statusText.width + 10
            height: 18
            color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
            border.width: 1
            border.color: root.anime.status === "ONGOING"
                ? Qt.rgba(0.220, 0.973, 0.573, 0.7)
                : Qt.rgba(0.435, 0.506, 0.447, 0.5)
            Text {
                id: statusText
                anchors.centerIn: parent
                text: root.anime.status || ""
                color: root.anime.status === "ONGOING" ? RetroTheme.primary : RetroTheme.mutedForeground
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
            text: root.anime.title || ""
            color: hoverArea.containsMouse ? RetroTheme.primary : RetroTheme.foreground
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            elide: Text.ElideRight
            Behavior on color { ColorAnimation { duration: 150 } }
        }
        Text {
            width: parent.width
            text: (root.anime.year || "") + " · EP " + (root.anime.episodes || 0)
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
