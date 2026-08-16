import QtQuick
import "retroData.js" as RetroData

Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    signal openAnime(string id)

    readonly property var library: RetroData.library()
    property string activeTab: "WATCHING"

    readonly property var tabs: [
        { id: "WATCHING", label: "Watching" },
        { id: "COMPLETED", label: "Completed" },
        { id: "ON HOLD", label: "On Hold" }
    ]

    function countFor(tabId) {
        var n = 0
        for (var i = 0; i < library.length; ++i)
            if (library[i].status === tabId) n++
        return n
    }

    readonly property var entries: {
        var out = []
        for (var i = 0; i < library.length; ++i)
            if (library[i].status === root.activeTab) out.push(library[i])
        return out
    }

    Column {
        id: contentColumn
        width: root.width
        topPadding: 20
        bottomPadding: 24
        spacing: 20

        Column {
            x: 20
            width: parent.width - 40
            spacing: 4
            bottomPadding: 8

            Text {
                text: "BOOKMARKS"
                font.family: RetroTheme.fontFamily
                font.bold: true
                font.pixelSize: 20
                color: RetroTheme.primary
            }
            Text {
                text: "// ~/library — " + root.library.length + " entries saved to disk"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        // === FILTER TABS ===
        Row {
            x: 20
            spacing: 0

            Repeater {
                model: root.tabs
                delegate: Rectangle {
                    readonly property bool isActive: root.activeTab === modelData.id
                    height: 38
                    width: labelRow.width + 24
                    color: isActive ? RetroTheme.primary : RetroTheme.card
                    border.width: 1
                    border.color: RetroTheme.border

                    Row {
                        id: labelRow
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            text: modelData.label.toUpperCase()
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 11
                            color: isActive ? RetroTheme.primaryForeground : (tabMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground)
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: countText.width + 6
                            height: 15
                            color: "transparent"
                            border.width: 1
                            border.color: isActive ? Qt.rgba(0.020, 0.075, 0.039, 0.4) : RetroTheme.border
                            Text {
                                id: countText
                                anchors.centerIn: parent
                                text: (root.countFor(modelData.id) < 10 ? "0" : "") + root.countFor(modelData.id)
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 9
                                color: isActive ? RetroTheme.primaryForeground : RetroTheme.mutedForeground
                            }
                        }
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = modelData.id
                    }
                }
            }
        }

        // === EMPTY STATE ===
        Rectangle {
            x: 20
            width: parent.width - 40
            height: 90
            visible: root.entries.length === 0
            color: "transparent"
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: "// no titles in this queue"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
        }

        // === GRID ===
        Flow {
            x: 20
            width: parent.width - 40
            spacing: 12
            visible: root.entries.length > 0

            Repeater {
                model: root.entries
                delegate: Rectangle {
                    readonly property var anime: RetroData.getAnime(modelData.animeId)
                    readonly property int pct: Math.round((modelData.watched / anime.episodes) * 100)

                    width: 220
                    height: 220 * 1.5 + 62
                    color: RetroTheme.card
                    border.width: 1
                    border.color: bmHover.containsMouse ? RetroTheme.primary : RetroTheme.border
                    Behavior on border.color { ColorAnimation { duration: 150 } }

                    Item {
                        id: bmCover
                        width: parent.width
                        height: parent.width * 1.5
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: anime.cover
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.6; color: "transparent" }
                                GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.8) }
                            }
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 4
                            width: bmRatingRow.width + 10
                            height: 18
                            color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
                            border.width: 1
                            border.color: Qt.rgba(1, 0.702, 0.169, 0.7)
                            Row {
                                id: bmRatingRow
                                anchors.centerIn: parent
                                spacing: 3
                                Text { text: "★"; color: RetroTheme.accent; font.pixelSize: 9 }
                                Text { text: anime.rating.toFixed(1); color: RetroTheme.accent; font.family: RetroTheme.fontFamily; font.pixelSize: 10 }
                            }
                        }
                    }

                    Rectangle { anchors.top: bmCover.bottom; width: parent.width; height: 1; color: RetroTheme.border }

                    Column {
                        anchors.top: bmCover.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 8
                        anchors.topMargin: 9
                        spacing: 6

                        Text {
                            width: parent.width
                            text: anime.title
                            color: bmHover.containsMouse ? RetroTheme.primary : RetroTheme.foreground
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }

                        Row {
                            width: parent.width
                            Text {
                                text: "EP " + (modelData.watched < 10 ? "0" : "") + modelData.watched + "/" + (anime.episodes < 10 ? "0" : "") + anime.episodes
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 9
                                color: RetroTheme.mutedForeground
                            }
                            Item { width: parent.width - 130; height: 1 }
                            Text {
                                text: pct + "%"
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 9
                                color: RetroTheme.primary
                            }
                        }

                        // Блочный прогресс-бар — 12 сегментов
                        Row {
                            width: parent.width
                            spacing: 2
                            Repeater {
                                model: 12
                                delegate: Rectangle {
                                    readonly property bool filled: index < Math.round((pct / 100) * 12)
                                    width: (parent.width - 11 * 2) / 12
                                    height: 8
                                    color: filled ? RetroTheme.primary : "transparent"
                                    border.width: filled ? 0 : 1
                                    border.color: RetroTheme.border
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: bmHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openAnime(anime.id)
                    }
                }
            }
        }
    }
}
