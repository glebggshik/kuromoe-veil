import QtQuick
import AnimeClient 1.0
import QtQuick.Effects
import QtQuick.Window

// Порт components/home-screen.tsx: hero-баннер со сканлайнами и glow-текстом,
// терминальная строка поиска, горизонтальная лента "Ongoing Anime".
// Данные реальные — через CatalogBridge (тот же C++ мост, что у обычной
// темы), не статичный мок.
Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    property bool tabActive: true
    property Window appWindow: null
    signal openAnime(var item)

    property var heroData: ({})
    property var cardsModel: []
    property bool searchActive: false
    property string heroDisplaySource: ""

    readonly property string heroBannerUrl: (root.heroData && root.heroData.heroBanner) ? String(root.heroData.heroBanner) : ""
    function posterOk(u) { return u && u.length > 0 && u.indexOf("/missing_") < 0 }

    function refreshHeroImage() {
        if (!posterOk(root.heroBannerUrl)) {
            root.heroDisplaySource = ""
            return
        }
        const cached = posterCache.cachedFile(root.heroBannerUrl)
        if (cached.length > 0) {
            root.heroDisplaySource = cached
            return
        }
        if (posterCache.isRemotePoster(root.heroBannerUrl))
            posterCache.requestPriority(root.heroBannerUrl)
        // Пока не скачано — без прокси Image потянет https напрямую, с прокси ждём кэш.
        root.heroDisplaySource = appConfig.proxyEnabled ? "" : root.heroBannerUrl
    }
    onHeroBannerUrlChanged: refreshHeroImage()

    Connections {
        target: posterCache
        function onPosterReady(remoteUrl, fileUrl) {
            if (remoteUrl === root.heroBannerUrl)
                root.heroDisplaySource = fileUrl
        }
    }

    CatalogBridge {
        id: bridge
        onHeroReady: function(item) { root.heroData = item || {} }
        onResultsReady: function(items) {
            root.cardsModel = items
            posterCache.preloadCatalog(items)
        }
        onError: function(msg) { console.warn("Retro Home:", msg) }
        Component.onCompleted: {
            loadHero()
            loadOngoing()
        }
    }

    // Ещё два списка на главной, как на MyAnimeList (Top Airing/Top
    // Upcoming/Most Popular) — отдельные бриджи, чтобы не перетирать друг
    // друга через общий resultsReady (тот же приём, что и bridge/bridgeAnnounced
    // в RetroBrowse.qml).
    property var upcomingModel: []
    property var popularModel: []

    CatalogBridge {
        id: bridgeUpcoming
        onResultsReady: function(items) {
            root.upcomingModel = items
            posterCache.preloadCatalog(items)
        }
        onError: function(msg) { console.warn("Retro Home (upcoming):", msg) }
        Component.onCompleted: loadAnnounced()
    }

    CatalogBridge {
        id: bridgePopular
        onResultsReady: function(items) {
            root.popularModel = items
            posterCache.preloadCatalog(items)
        }
        onError: function(msg) { console.warn("Retro Home (popular):", msg) }
        Component.onCompleted: loadPopular()
    }

    // Компактная горизонтальная лента для TOP UPCOMING / MOST POPULAR — то же
    // визуальное оформление, что и у ONGOING ANIME выше, но без переключения
    // на сетку в развёрнутом окне (там эта логика завязана именно на
    // "онгоинги", усложнять остальные секции тем же самым не нужно).
    component SimpleCarousel: Column {
        id: simpleCarouselRoot
        property string sectionTitle: ""
        property var itemsModel: []
        signal cardClicked(var item)

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
                    text: simpleCarouselRoot.sectionTitle
                    font.family: RetroTheme.fontFamily
                    font.bold: true
                    font.pixelSize: 12
                    color: RetroTheme.foreground
                }
                Text {
                    text: "[" + simpleCarouselRoot.itemsModel.length + "]"
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
                    border.color: simpleLeftArrow.containsMouse ? RetroTheme.primary : RetroTheme.border
                    Text { anchors.centerIn: parent; text: "‹"; font.pixelSize: 14; color: simpleLeftArrow.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                    MouseArea { id: simpleLeftArrow; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: simpleCarouselList.contentX = Math.max(0, simpleCarouselList.contentX - 360) }
                }
                Rectangle {
                    width: 30; height: 30
                    color: "transparent"
                    border.width: 1
                    border.color: simpleRightArrow.containsMouse ? RetroTheme.primary : RetroTheme.border
                    Text { anchors.centerIn: parent; text: "›"; font.pixelSize: 14; color: simpleRightArrow.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                    MouseArea { id: simpleRightArrow; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: simpleCarouselList.contentX = Math.min(simpleCarouselList.contentWidth - simpleCarouselList.width, simpleCarouselList.contentX + 360) }
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: RetroTheme.border }
        }

        ListView {
            id: simpleCarouselList
            width: parent.width
            height: 168 * 1.5 + 46
            orientation: ListView.Horizontal
            spacing: 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: simpleCarouselRoot.itemsModel
            delegate: RetroCard {
                anime: modelData
                imagesEnabled: root.tabActive
                onClicked: simpleCarouselRoot.cardClicked(modelData)
            }
        }
    }

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
            visible: !!(root.heroData && root.heroData.title)

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
                source: root.heroDisplaySource
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
                        text: (root.heroData && root.heroData.continuing) ? "CONTINUE // WATCHING" : "FEATURED // NOW STREAMING"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 9
                        color: RetroTheme.accent
                    }
                }

                Text {
                    id: heroTitle
                    text: (root.heroData && root.heroData.title) || ""
                    font.family: RetroTheme.fontFamily
                    font.bold: true
                    font.pixelSize: 32
                    color: RetroTheme.primary
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    width: parent.width
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
                    text: (root.heroData && root.heroData.description) || ""
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
                        visible: (root.heroData && root.heroData.score || 0) > 0
                        width: ratingBadge.width + 12; height: 20
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(1, 0.702, 0.169, 0.6)
                        Row {
                            id: ratingBadge
                            anchors.centerIn: parent
                            spacing: 3
                            Text { text: "★"; color: RetroTheme.accent; font.pixelSize: 10 }
                            Text {
                                text: (root.heroData && root.heroData.score ? Number(root.heroData.score).toFixed(1) : "")
                                color: RetroTheme.accent
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 10
                            }
                        }
                    }
                    Repeater {
                        model: (root.heroData && root.heroData.genreTags) || []
                        delegate: Rectangle {
                            width: genreText.width + 12; height: 20
                            color: "transparent"
                            border.width: 1
                            border.color: RetroTheme.border
                            Text {
                                id: genreText
                                anchors.centerIn: parent
                                text: (modelData.name || "").toUpperCase()
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
                            text: (root.heroData && root.heroData.continuing) ? "CONTINUE" : "WATCH NOW"
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
                        onClicked: if (root.heroData && root.heroData.id) root.openAnime(root.heroData)
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
                        onAccepted: {
                            const q = text.trim()
                            if (q.length === 0) {
                                root.searchActive = false
                                bridge.loadOngoing()
                                return
                            }
                            root.searchActive = true
                            bridge.search(q)
                        }
                    }
                }
            }
        }

        // === CAROUSEL (онгоинги или результаты поиска) ===
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
                        text: root.searchActive ? "SEARCH RESULTS" : "TOP AIRING ANIME"
                        font.family: RetroTheme.fontFamily
                        font.bold: true
                        font.pixelSize: 12
                        color: RetroTheme.foreground
                    }
                    Text {
                        text: "[" + root.cardsModel.length + "]"
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
                model: root.cardsModel
                delegate: RetroCard {
                    anime: modelData
                    imagesEnabled: root.tabActive
                    onClicked: root.openAnime(modelData)
                }
            }
        }

        // === TOP UPCOMING / MOST POPULAR — как на MyAnimeList ===
        SimpleCarousel {
            x: 20
            width: parent.width - 40
            visible: !root.searchActive
            sectionTitle: "TOP UPCOMING ANIME"
            itemsModel: root.upcomingModel
            onCardClicked: function(item) { root.openAnime(item) }
        }

        SimpleCarousel {
            x: 20
            width: parent.width - 40
            visible: !root.searchActive
            sectionTitle: "MOST POPULAR ANIME"
            itemsModel: root.popularModel
            onCardClicked: function(item) { root.openAnime(item) }
        }
    }
}
