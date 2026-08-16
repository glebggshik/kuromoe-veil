import QtQuick
import AnimeClient 1.0

// Порт components/bookmarks-screen.tsx — реальные закладки через
// BookmarksBridge (те же 5 статусов, что у обычной BookmarksView.qml), не
// статичный мок. Прогресс серии — через historyManager (SQLite), отдельно
// от статуса закладки.
Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    property bool tabActive: true
    signal openAnime(var item)

    readonly property var tabs: [
        { value: "watching", label: "Watching" },
        { value: "planned", label: "Planned" },
        { value: "watched", label: "Watched" },
        { value: "postponed", label: "Postponed" },
        { value: "dropped", label: "Dropped" }
    ]
    property string currentTab: "watching"
    property var cardsModel: []
    property bool initialLoadDone: false

    BookmarksBridge {
        id: bridge
        onResultsReady: function(items) {
            root.cardsModel = items
            posterCache.preloadCatalog(items)
        }
        onError: function(msg) { console.warn("Retro Bookmarks:", msg) }
    }

    function ensureLoaded() {
        if (root.initialLoadDone)
            return
        root.initialLoadDone = true
        bridge.loadStatus(root.currentTab)
    }

    function reload() { bridge.loadStatus(root.currentTab) }

    function selectTab(value) {
        root.currentTab = value
        reload()
    }

    Component.onCompleted: ensureLoaded()

    // Прогресс просмотра (эпизод) — из HistoryManager, по id тайтла.
    function watchedEpisodeFor(id) {
        const p = historyManager.loadProgress(String(id))
        return (p && p.found) ? p.episode : 0
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
                text: "// ~/library — " + root.cardsModel.length + " entries in " + root.currentTab
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        // === FILTER TABS ===
        Flow {
            x: 20
            width: parent.width - 40
            spacing: 0

            Repeater {
                model: root.tabs
                delegate: Rectangle {
                    readonly property bool isActive: root.currentTab === modelData.value
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
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectTab(modelData.value)
                    }
                }
            }
        }

        // === EMPTY STATE ===
        Rectangle {
            x: 20
            width: parent.width - 40
            height: 90
            visible: root.cardsModel.length === 0
            color: "transparent"
            border.width: 1
            border.color: RetroTheme.border
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
            visible: root.cardsModel.length > 0

            Repeater {
                model: root.cardsModel
                delegate: Rectangle {
                    id: bmCard
                    readonly property var anime: modelData
                    readonly property string posterSource: {
                        const hd = anime.posterHd || ""
                        const p = anime.poster || ""
                        function ok(u) { return u.length > 0 && u.indexOf("/missing_") < 0 }
                        if (ok(hd)) return hd
                        if (ok(p)) return p
                        return hd || p
                    }
                    readonly property int watched: root.watchedEpisodeFor(anime.id)
                    // anime.episodes — общее число серий, 0 пока тайтл онгоинг
                    // (Шикимори ещё не знает финальное число). Для таких берём
                    // episodesAired (сколько вышло на сейчас) — иначе карточка
                    // показывает "EP 01/00" вместо реального прогресса.
                    readonly property int totalEp: anime.episodes || anime.episodesAired || 0
                    readonly property int pct: totalEp > 0 ? Math.min(100, Math.round((watched / totalEp) * 100)) : 0

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

                        PosterThumbnail {
                            anchors.fill: parent
                            posterSource: bmCard.posterSource
                            posterActive: root.tabActive
                            cornerRadius: 0
                            placeholderLetter: (anime.title || "?").trim().charAt(0).toUpperCase() || "?"
                        }
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.6; color: "transparent" }
                                GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.8) }
                            }
                        }
                        Rectangle {
                            visible: (anime.score || 0) > 0
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
                                Text {
                                    text: Number(anime.score || 0).toFixed(1)
                                    color: RetroTheme.accent
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 10
                                }
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
                            text: anime.title || "?"
                            color: bmHover.containsMouse ? RetroTheme.primary : RetroTheme.foreground
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }

                        Row {
                            width: parent.width
                            Text {
                                text: "EP " + (bmCard.watched < 10 ? "0" : "") + bmCard.watched + "/"
                                    + (bmCard.totalEp > 0 ? ((bmCard.totalEp < 10 ? "0" : "") + bmCard.totalEp) : "??")
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 9
                                color: RetroTheme.mutedForeground
                            }
                            Item { width: parent.width - 130; height: 1 }
                            Text {
                                text: bmCard.pct + "%"
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
                                    readonly property bool filled: index < Math.round((bmCard.pct / 100) * 12)
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
                        onClicked: root.openAnime(anime)
                    }
                }
            }
        }
    }
}
