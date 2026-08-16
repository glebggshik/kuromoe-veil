import QtQuick
import QtQuick.Effects
import "retroData.js" as RetroData

Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    property string animeId: "cherry-blade"
    signal openAnime(string id)
    signal backRequested()

    readonly property var anime: RetroData.getAnime(root.animeId)
    readonly property var related: {
        var all = RetroData.items()
        var out = []
        for (var i = 0; i < all.length; ++i)
            if (all[i].id !== root.anime.id) out.push(all[i])
        return out
    }
    readonly property var sources: RetroData.sources()
    readonly property var voiceovers: RetroData.voiceovers()
    property int episode: 1

    function setEp(n) { root.episode = Math.max(1, Math.min(root.anime.episodes, n)) }
    onAnimeIdChanged: root.episode = 1

    Column {
        id: contentColumn
        width: root.width
        spacing: 0

        // === HEADER: фоновая картинка + постер, наложенный поверх ===
        Item {
            id: headerBlock
            width: parent.width
            height: Math.max(300, 300 - 190 + infoRow.height)

            Item {
                id: bgArea
                width: parent.width
                height: 300
                clip: true

                Image {
                    anchors.fill: parent
                    source: "assets/retro/detail-bg.png"
                    fillMode: Image.PreserveAspectCrop
                    opacity: 0.4
                    asynchronous: true
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: RetroTheme.background }
                        GradientStop { position: 0.5; color: Qt.rgba(0.024, 0.031, 0.027, 0.7) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.3) }
                    }
                }
                ScanlinesOverlay { anchors.fill: parent }
            }

            Rectangle {
                x: 16; y: 16
                width: backRow.width + 24
                height: 30
                color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
                border.width: 1
                border.color: backMouse.containsMouse ? RetroTheme.primary : RetroTheme.border
                z: 3

                Row {
                    id: backRow
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "←"; font.pixelSize: 12; color: backMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                    Text {
                        text: "BACK"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 10
                        color: backMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground
                    }
                }
                MouseArea {
                    id: backMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }
            }

            Row {
                id: infoRow
                x: 20
                y: 300 - 190
                width: parent.width - 40
                spacing: 20
                z: 2

                Rectangle {
                    id: posterBox
                    width: 190
                    height: 190 * 1.5
                    color: RetroTheme.card
                    border.width: 1
                    border.color: RetroTheme.primary

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: RetroTheme.primary
                        shadowBlur: 0.5
                        shadowOpacity: 0.35
                    }

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: "assets/retro/poster-main.png"
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 8
                        width: posterRating.width + 10
                        height: 18
                        color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
                        border.width: 1
                        border.color: Qt.rgba(1, 0.702, 0.169, 0.7)
                        Row {
                            id: posterRating
                            anchors.centerIn: parent
                            spacing: 3
                            Text { text: "★"; color: RetroTheme.accent; font.pixelSize: 9 }
                            Text { text: root.anime.rating.toFixed(1); color: RetroTheme.accent; font.family: RetroTheme.fontFamily; font.pixelSize: 10 }
                        }
                    }
                }

                Column {
                    width: parent.width - posterBox.width - parent.spacing
                    topPadding: 100
                    spacing: 12

                    Column {
                        width: parent.width
                        spacing: 2
                        Text {
                            text: root.anime.status + " · " + root.anime.year + " · " + root.anime.episodes + " EPISODES"
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 10
                            color: RetroTheme.accent
                        }
                        Text {
                            text: root.anime.title
                            font.family: RetroTheme.fontFamily
                            font.bold: true
                            font.pixelSize: 24
                            color: RetroTheme.primary
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                shadowEnabled: true
                                shadowColor: RetroTheme.primary
                                shadowBlur: 0.7
                                shadowOpacity: 0.6
                            }
                        }
                    }

                    Text {
                        width: Math.min(parent.width, 640)
                        text: root.anime.synopsis
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 12
                        color: Qt.rgba(0.812, 0.910, 0.824, 0.85)
                        wrapMode: Text.WordWrap
                    }

                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: root.anime.genres
                            delegate: Rectangle {
                                width: genreTag.width + 14
                                height: 22
                                color: Qt.rgba(0.220, 0.973, 0.573, 0.1)
                                border.width: 1
                                border.color: Qt.rgba(0.220, 0.973, 0.573, 0.5)
                                Text {
                                    id: genreTag
                                    anchors.centerIn: parent
                                    text: "#" + modelData
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 9
                                    color: RetroTheme.primary
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 10
                        Rectangle {
                            width: playRow.width + 32
                            height: 36
                            color: playMouse.containsMouse ? "transparent" : RetroTheme.primary
                            border.width: 1
                            border.color: RetroTheme.primary
                            Behavior on color { ColorAnimation { duration: 120 } }
                            layer.enabled: playMouse.containsMouse
                            layer.effect: MultiEffect {
                                shadowEnabled: true
                                shadowColor: RetroTheme.primary
                                shadowBlur: 0.6
                                shadowOpacity: 0.5
                            }
                            Row {
                                id: playRow
                                anchors.centerIn: parent
                                spacing: 8
                                Text { text: "▶"; font.pixelSize: 11; color: playMouse.containsMouse ? RetroTheme.primary : RetroTheme.primaryForeground }
                                Text {
                                    text: "PLAY EP " + (root.episode < 10 ? "0" : "") + root.episode
                                    font.family: RetroTheme.fontFamily
                                    font.bold: true
                                    font.pixelSize: 11
                                    color: playMouse.containsMouse ? RetroTheme.primary : RetroTheme.primaryForeground
                                }
                            }
                            MouseArea { id: playMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
                        }

                        Rectangle {
                            width: bookmarkRow.width + 28
                            height: 36
                            color: RetroTheme.card
                            border.width: 1
                            border.color: bmMouse.containsMouse ? RetroTheme.accent : RetroTheme.border
                            Row {
                                id: bookmarkRow
                                anchors.centerIn: parent
                                spacing: 8
                                Text { text: "+"; font.pixelSize: 13; color: bmMouse.containsMouse ? RetroTheme.accent : RetroTheme.mutedForeground }
                                Text {
                                    text: "BOOKMARK"
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 11
                                    color: bmMouse.containsMouse ? RetroTheme.accent : RetroTheme.mutedForeground
                                }
                            }
                            MouseArea { id: bmMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
                        }
                    }
                }
            }
        }

        // === PLAYER CONTROLS ===
        Column {
            x: 20
            width: parent.width - 40
            topPadding: 24
            spacing: 14

            Row {
                width: parent.width
                spacing: 12
                RetroSelect {
                    width: (parent.width - 12) / 2
                    label: "Select Source"
                    options: root.sources
                }
                RetroSelect {
                    width: (parent.width - 12) / 2
                    label: "Select Voiceover"
                    options: root.voiceovers
                }
            }

            Rectangle {
                width: parent.width
                height: epColumn.height
                color: RetroTheme.card
                border.width: 1
                border.color: RetroTheme.border

                Column {
                    id: epColumn
                    width: parent.width

                    Rectangle {
                        width: parent.width; height: 1; color: RetroTheme.border
                    }
                    Text {
                        x: 12; topPadding: 8; bottomPadding: 8
                        text: "// episode selector"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 9
                        color: RetroTheme.mutedForeground
                    }
                    Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

                    Row {
                        width: epColumn.width
                        height: 56

                        Rectangle {
                            width: 90; height: parent.height
                            color: prevMouse.containsMouse && root.episode > 1 ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                            opacity: root.episode > 1 ? 1 : 0.3
                            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: RetroTheme.border }
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Text { text: "‹"; font.pixelSize: 14; color: prevMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                                Text { text: "PREV"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: prevMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                            }
                            MouseArea {
                                id: prevMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.episode > 1
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setEp(root.episode - 1)
                            }
                        }

                        Row {
                            width: epColumn.width - 180
                            height: parent.height
                            spacing: 12
                            Item { width: (parent.width - epBox.width - epSuffix.width - 24) / 2; height: 1 }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "EP"; font.family: RetroTheme.fontFamily; font.pixelSize: 11; color: RetroTheme.mutedForeground }
                            Rectangle {
                                id: epBox
                                anchors.verticalCenter: parent.verticalCenter
                                width: 64; height: 34
                                color: RetroTheme.background
                                border.width: 1
                                border.color: epSpin.activeFocus ? RetroTheme.primary : RetroTheme.border
                                TextInput {
                                    id: epSpin
                                    anchors.fill: parent
                                    horizontalAlignment: TextInput.AlignHCenter
                                    verticalAlignment: TextInput.AlignVCenter
                                    text: root.episode
                                    validator: IntValidator { bottom: 1; top: root.anime.episodes }
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 15
                                    color: RetroTheme.primary
                                    selectByMouse: true
                                    onEditingFinished: root.setEp(parseInt(text) || 1)
                                }
                            }
                            Text {
                                id: epSuffix
                                anchors.verticalCenter: parent.verticalCenter
                                text: "/ " + (root.anime.episodes < 10 ? "0" : "") + root.anime.episodes
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 11
                                color: RetroTheme.mutedForeground
                            }
                        }

                        Rectangle {
                            width: 90; height: parent.height
                            color: nextMouse.containsMouse && root.episode < root.anime.episodes ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                            opacity: root.episode < root.anime.episodes ? 1 : 0.3
                            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: RetroTheme.border }
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Text { text: "NEXT"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: nextMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                                Text { text: "›"; font.pixelSize: 14; color: nextMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                            }
                            MouseArea {
                                id: nextMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.episode < root.anime.episodes
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setEp(root.episode + 1)
                            }
                        }
                    }
                }
            }
        }

        // === RELATED TITLES ===
        Column {
            x: 20
            width: parent.width - 40
            topPadding: 24
            bottomPadding: 24
            spacing: 10

            Row {
                width: parent.width
                spacing: 8
                bottomPadding: 8
                Rectangle { width: 3; height: 12; color: RetroTheme.accent; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: "RELATED TITLES"
                    font.family: RetroTheme.fontFamily
                    font.bold: true
                    font.pixelSize: 12
                    color: RetroTheme.foreground
                }
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

            ListView {
                width: parent.width
                height: 168 * 1.5 + 46
                orientation: ListView.Horizontal
                spacing: 12
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.related
                delegate: RetroCard {
                    anime: modelData
                    onClicked: root.openAnime(modelData.id)
                }
            }
        }
    }
}
