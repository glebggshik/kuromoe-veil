import QtQuick
import QtQuick.Effects
import "retroData.js" as RetroData

// Порт components/home-screen.tsx: hero-баннер со сканлайнами и glow-текстом,
// терминальная строка поиска, горизонтальная лента "Ongoing Anime".
Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    signal openAnime(string id)

    readonly property var heroData: RetroData.hero()
    readonly property var animeItems: RetroData.items()
    readonly property var ongoing: animeItems.filter(function(a) { return a.status === "ONGOING" })

    Column {
        id: contentColumn
        width: root.width
        topPadding: 20
        bottomPadding: 24
        spacing: 28

        // === HERO ===
        Item {
            id: hero
            x: 20
            width: parent.width - 40
            height: 280

            Rectangle {
                anchors.fill: parent
                border.width: 1
                border.color: RetroTheme.border
                color: "transparent"
                z: 2
            }

            Image {
                id: heroImg
                anchors.fill: parent
                source: root.heroData.banner
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.95) }
                    GradientStop { position: 0.45; color: Qt.rgba(0.024, 0.031, 0.027, 0.55) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.85) }
                }
            }

            ScanlinesOverlay { anchors.fill: parent }

            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 32
                width: Math.min(520, hero.width - 64)
                spacing: 10

                Rectangle {
                    width: featuredText.width + 16
                    height: 22
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(1, 0.702, 0.169, 0.7)
                    Text {
                        id: featuredText
                        anchors.centerIn: parent
                        text: "FEATURED // NOW STREAMING"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 9
                        color: RetroTheme.accent
                    }
                }

                Text {
                    id: heroTitle
                    text: root.heroData.title
                    font.family: RetroTheme.fontFamily
                    font.bold: true
                    font.pixelSize: 32
                    color: RetroTheme.primary
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: RetroTheme.primary
                        shadowBlur: 0.7
                        shadowOpacity: 0.6
                    }
                }

                Text {
                    width: parent.width
                    text: root.heroData.synopsis
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 12
                    color: Qt.rgba(0.812, 0.910, 0.824, 0.8)
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                Row {
                    spacing: 6
                    Rectangle {
                        width: ratingBadge.width + 12; height: 20
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(1, 0.702, 0.169, 0.6)
                        Row {
                            id: ratingBadge
                            anchors.centerIn: parent
                            spacing: 3
                            Text { text: "★"; color: RetroTheme.accent; font.pixelSize: 10 }
                            Text { text: root.heroData.rating.toFixed(1); color: RetroTheme.accent; font.family: RetroTheme.fontFamily; font.pixelSize: 10 }
                        }
                    }
                    Repeater {
                        model: root.heroData.genres
                        delegate: Rectangle {
                            width: genreText.width + 12; height: 20
                            color: "transparent"
                            border.width: 1
                            border.color: RetroTheme.border
                            Text {
                                id: genreText
                                anchors.centerIn: parent
                                text: modelData
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 9
                                color: RetroTheme.mutedForeground
                            }
                        }
                    }
                }

                Rectangle {
                    width: watchBtnRow.width + 32
                    height: 36
                    color: watchMouse.containsMouse ? "transparent" : RetroTheme.primary
                    border.width: 1
                    border.color: RetroTheme.primary
                    Behavior on color { ColorAnimation { duration: 120 } }

                    layer.enabled: watchMouse.containsMouse
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: RetroTheme.primary
                        shadowBlur: 0.6
                        shadowOpacity: 0.5
                    }

                    Row {
                        id: watchBtnRow
                        anchors.centerIn: parent
                        spacing: 8
                        Text { text: "▶"; font.pixelSize: 12; color: watchMouse.containsMouse ? RetroTheme.primary : RetroTheme.primaryForeground }
                        Text {
                            text: "WATCH NOW"
                            font.family: RetroTheme.fontFamily
                            font.bold: true
                            font.pixelSize: 11
                            color: watchMouse.containsMouse ? RetroTheme.primary : RetroTheme.primaryForeground
                        }
                    }
                    MouseArea {
                        id: watchMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (root.animeItems.length > 0) root.openAnime(root.animeItems[0].id)
                    }
                }
            }
        }

        // === TERMINAL SEARCH ===
        Column {
            x: 20
            width: parent.width - 40
            spacing: 8

            Text {
                text: "// search database"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.mutedForeground
            }

            Rectangle {
                width: parent.width
                height: 42
                color: RetroTheme.card
                border.width: 1
                border.color: searchInput.activeFocus ? RetroTheme.primary : RetroTheme.border

                layer.enabled: searchInput.activeFocus
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: RetroTheme.primary
                    shadowBlur: 0.5
                    shadowOpacity: 0.4
                }

                Row {
                    anchors.fill: parent
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        height: parent.height
                        spacing: 6
                        leftPadding: 12
                        rightPadding: 12
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "⌕"; font.pixelSize: 14; color: RetroTheme.primary }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "grep"; font.family: RetroTheme.fontFamily; font.pixelSize: 11; color: RetroTheme.primary }
                        Rectangle { width: 1; height: parent.height; color: RetroTheme.border }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 10
                        text: ">"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 13
                        color: RetroTheme.primary
                    }
                    TextInput {
                        id: searchInput
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 160
                        leftPadding: 8
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 12
                        color: RetroTheme.foreground
                        selectByMouse: true
                        Text {
                            visible: searchInput.text.length === 0
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.leftPadding
                            text: "search titles, genres, studios..."
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 12
                            color: Qt.rgba(0.435, 0.506, 0.447, 0.7)
                        }
                    }
                }
            }
        }

        // === ONGOING CAROUSEL ===
        Column {
            x: 20
            width: parent.width - 40
            spacing: 10

            Item {
                width: parent.width
                height: 28

                Row {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    spacing: 8
                    Rectangle { width: 3; height: 12; color: RetroTheme.primary; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: "ONGOING ANIME"
                        font.family: RetroTheme.fontFamily
                        font.bold: true
                        font.pixelSize: 12
                        color: RetroTheme.foreground
                    }
                    Text {
                        text: "[" + root.ongoing.length + "]"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        color: RetroTheme.mutedForeground
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    spacing: 4

                    Rectangle {
                        width: 30; height: 30
                        color: "transparent"
                        border.width: 1
                        border.color: leftArrow.containsMouse ? RetroTheme.primary : RetroTheme.border
                        Text { anchors.centerIn: parent; text: "‹"; font.pixelSize: 14; color: leftArrow.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                        MouseArea { id: leftArrow; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: carousel.contentX = Math.max(0, carousel.contentX - 360) }
                    }
                    Rectangle {
                        width: 30; height: 30
                        color: "transparent"
                        border.width: 1
                        border.color: rightArrow.containsMouse ? RetroTheme.primary : RetroTheme.border
                        Text { anchors.centerIn: parent; text: "›"; font.pixelSize: 14; color: rightArrow.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                        MouseArea { id: rightArrow; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: carousel.contentX = Math.min(carousel.contentWidth - carousel.width, carousel.contentX + 360) }
                    }
                }

                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: RetroTheme.border }
            }

            ListView {
                id: carousel
                width: parent.width
                height: 168 * 1.5 + 46
                orientation: ListView.Horizontal
                spacing: 12
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.ongoing
                delegate: RetroCard {
                    anime: modelData
                    onClicked: root.openAnime(modelData.id)
                }
            }
        }
    }
}
