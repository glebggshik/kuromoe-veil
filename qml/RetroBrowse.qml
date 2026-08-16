import QtQuick
import QtQuick.Controls
import AnimeClient 1.0

// Порт components/browse-screen.tsx — данные реальные, через CatalogBridge
// (popular/latest, как в обычной BrowseView.qml), не статичный мок.
// Плюс жанры/формат/год фильтров и пагинация — 1:1 с обычной темой
// (BrowseView.qml), которых тут раньше не было вовсе.
Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    property bool tabActive: true
    signal openAnime(var item)

    readonly property var tabs: [
        { key: "popular", label: "POPULAR" },
        { key: "latest", label: "LATEST" }
    ]
    property string currentTab: "popular"
    property var cardsModel: []
    property bool initialLoadDone: false

    // Анонсы (status=anons) — отдельная секция, отдельный запрос через свой
    // экземпляр бриджа (bridgeAnnounced ниже), не подмешаны в cardsModel.
    // Раньше order=aired_on без фильтра статуса сортировал по ДАТЕ ВЫХОДА, а
    // не по тому, вышло ли что-то уже — анонсы с датой в 2027-2028 обгоняли
    // реально вышедшее и вытесняли его со страницы 1 почти целиком.
    property var announcedModel: []

    property bool filtersOpen: false
    // Явный forceActiveFocus() вместо декларативного "focus: filtersOpen":
    // клик по пилюле FILTERS не переносит клавиатурный фокус (MouseArea это
    // не делает сама), так что декларативное присваивание не гарантированно
    // выигрывает активный фокус в этот момент — принудительно забираем его.
    onFiltersOpenChanged: if (filtersOpen) root.forceActiveFocus()
    property string filterKind: ""
    property var filterGenres: []
    property string filterYear: ""

    readonly property var kindOptions: [
        { value: "tv", label: "TV" },
        { value: "movie", label: "MOVIE" },
        { value: "ova", label: "OVA" },
        { value: "ona", label: "ONA" },
        { value: "special", label: "SPECIAL" },
        { value: "music", label: "CLIP" }
    ]
    readonly property var popularGenreIds: [1, 2, 4, 8, 10, 12, 14, 22, 24, 30, 36, 37, 7, 117, 18, 130, 38, 40, 35, 23, 19, 27, 42]
    property var allGenreOptions: []
    property bool showAllGenres: false

    readonly property var genreOptions: root.allGenreOptions.filter(function(g) {
        return root.popularGenreIds.indexOf(g.id) >= 0
    })
    readonly property var visibleGenreOptions: root.showAllGenres ? root.allGenreOptions : root.genreOptions

    readonly property bool hasActiveFilters: root.filterKind !== "" || root.filterGenres.length > 0 || root.filterYear !== ""

    function toggleGenre(id) {
        var arr = root.filterGenres.slice()
        var idx = arr.indexOf(id)
        if (idx >= 0) arr.splice(idx, 1)
        else arr.push(id)
        root.filterGenres = arr
    }

    function applyFilters() {
        bridge.applyFilters(root.filterKind, root.filterGenres, root.filterYear)
    }

    function resetFilters() {
        root.filterKind = ""
        root.filterGenres = []
        root.filterYear = ""
        reload()
    }

    // Esc закрывает открытую панель фильтров — как в обычной теме.
    // Keys.onEscapePressed + focus, а не Shortcut: тот же паттерн, что у
    // выхода из cinemaMode в RetroDetail.qml (см. cinemaLayer) — Shortcut с
    // sequence "Escape" в этом фреймлесс-окне не активируется надёжно.
    focus: root.filtersOpen
    Keys.onEscapePressed: function(event) {
        if (root.filtersOpen) {
            root.filtersOpen = false
            event.accepted = true
        }
    }

    CatalogBridge {
        id: bridge
        onResultsReady: function(items) {
            root.cardsModel = items
            posterCache.preloadCatalog(items)
            // Без этого после NEXT/PREV список меняется где-то ниже экрана —
            // пока сам не проскроллишь наверх, кажется, что кнопка не
            // сработала (хотя страница реально сменилась).
            root.contentY = 0
        }
        onError: function(msg) { console.warn("Retro Browse:", msg) }
    }

    CatalogBridge {
        id: bridgeAnnounced
        onResultsReady: function(items) {
            root.announcedModel = items
            posterCache.preloadCatalog(items)
        }
        onError: function(msg) { console.warn("Retro Browse (announced):", msg) }
    }

    function ensureLoaded() {
        if (root.initialLoadDone)
            return
        root.initialLoadDone = true
        root.allGenreOptions = bridge.allGenres()
        // Фильтры/вкладка приходят уже восстановленными из RetroShell (см.
        // browseComp) — если что-то было выбрано раньше, переигрываем через
        // applyFilters(), а не просто reload() (который их игнорирует).
        if (root.hasActiveFilters)
            applyFilters()
        else
            reload()
    }

    function reload() {
        root.announcedModel = []
        if (root.currentTab === "popular") {
            bridge.loadPopular()
        } else {
            bridge.loadLatest()
            bridgeAnnounced.loadAnnounced()
        }
    }

    function selectTab(key) {
        root.currentTab = key
        reload()
    }

    Component.onCompleted: ensureLoaded()

    component FilterPill: Rectangle {
        id: pillRoot
        property string text: ""
        property bool active: false
        signal clicked()

        width: pillLabel.width + 20
        height: 28
        color: active ? RetroTheme.primary : RetroTheme.card
        border.width: 1
        border.color: active ? RetroTheme.primary : (pillMouse.containsMouse ? RetroTheme.primary : RetroTheme.border)

        Text {
            id: pillLabel
            anchors.centerIn: parent
            text: pillRoot.text
            font.family: RetroTheme.fontFamily
            font.pixelSize: 10
            color: pillRoot.active ? RetroTheme.primaryForeground : (pillMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground)
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pillRoot.clicked()
        }
    }

    Column {
        id: contentColumn
        width: root.width
        topPadding: 20
        bottomPadding: 24
        spacing: 16

        Column {
            x: 20
            width: parent.width - 40
            spacing: 4
            bottomPadding: 4

            Text {
                text: "BROWSE"
                font.family: RetroTheme.fontFamily
                font.bold: true
                font.pixelSize: 20
                color: RetroTheme.primary
            }
            Text {
                text: bridge.loading
                    ? "// loading..."
                    : "// ls -la /catalog — " + root.cardsModel.length + " titles found — page " + bridge.page
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        Row {
            x: 20
            spacing: 8

            Repeater {
                model: root.tabs
                delegate: Rectangle {
                    readonly property bool isActive: root.currentTab === modelData.key
                    width: tabText.width + 24
                    height: 32
                    color: isActive ? RetroTheme.primary : RetroTheme.card
                    border.width: 1
                    border.color: RetroTheme.border
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: modelData.label
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        color: isActive ? RetroTheme.primaryForeground : (tabMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground)
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectTab(modelData.key)
                    }
                }
            }

            FilterPill {
                text: "FILTERS" + (root.hasActiveFilters ? " ●" : "") + " [ESC]"
                active: root.filtersOpen
                onClicked: root.filtersOpen = !root.filtersOpen
            }
        }

        Column {
            x: 20
            width: parent.width - 40
            visible: root.filtersOpen
            spacing: 10

            Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

            Text { text: "// формат"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.mutedForeground }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: root.kindOptions
                    delegate: FilterPill {
                        text: modelData.label
                        active: root.filterKind === modelData.value
                        onClicked: root.filterKind = (root.filterKind === modelData.value ? "" : modelData.value)
                    }
                }
            }

            Text { text: "// жанры"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.mutedForeground }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: root.visibleGenreOptions
                    delegate: FilterPill {
                        text: modelData.name
                        active: root.filterGenres.indexOf(modelData.id) >= 0
                        onClicked: root.toggleGenre(modelData.id)
                    }
                }
                FilterPill {
                    text: root.showAllGenres ? "СВЕРНУТЬ" : "ЕЩЁ ЖАНРЫ"
                    onClicked: root.showAllGenres = !root.showAllGenres
                }
            }

            Row {
                spacing: 10
                Text {
                    text: "// год"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: 90
                    height: 28
                    color: RetroTheme.card
                    border.width: 1
                    border.color: RetroTheme.border
                    TextField {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        text: root.filterYear
                        color: RetroTheme.foreground
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        background: Item {}
                        placeholderText: "2016"
                        placeholderTextColor: RetroTheme.mutedForeground
                        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                        onTextChanged: root.filterYear = text
                    }
                }
                FilterPill { text: "ПРИМЕНИТЬ"; active: true; onClicked: root.applyFilters() }
                FilterPill {
                    visible: root.hasActiveFilters
                    text: "СБРОСИТЬ"
                    onClicked: root.resetFilters()
                }
            }

            Rectangle { width: parent.width; height: 1; color: RetroTheme.border }
        }

        Flow {
            x: 20
            width: parent.width - 40
            spacing: 12

            Repeater {
                model: root.cardsModel
                delegate: RetroCard {
                    width: 168
                    anime: modelData
                    imagesEnabled: root.tabActive
                    onClicked: root.openAnime(modelData)
                }
            }
        }

        Column {
            x: 20
            width: parent.width - 40
            spacing: 10
            visible: root.announcedModel.length > 0

            Row {
                spacing: 8
                Rectangle { width: 3; height: 12; color: RetroTheme.accent; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: "АНОНСЫ"
                    font.family: RetroTheme.fontFamily
                    font.bold: true
                    font.pixelSize: 12
                    color: RetroTheme.foreground
                }
                Text {
                    text: "[" + root.announcedModel.length + "]"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 11
                    color: RetroTheme.mutedForeground
                }
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

            Flow {
                width: parent.width
                spacing: 12

                Repeater {
                    model: root.announcedModel
                    delegate: RetroCard {
                        width: 168
                        anime: modelData
                        imagesEnabled: root.tabActive
                        onClicked: root.openAnime(modelData)
                    }
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            height: 40
            spacing: 12

            FilterPill {
                text: "‹ PREV"
                active: false
                enabled: bridge.hasPrev
                opacity: bridge.hasPrev ? 1 : 0.35
                onClicked: bridge.hasPrev ? bridge.prevPage() : undefined
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "PAGE " + bridge.page
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            FilterPill {
                text: "NEXT ›"
                active: false
                enabled: bridge.hasNext
                opacity: bridge.hasNext ? 1 : 0.35
                onClicked: bridge.hasNext ? bridge.nextPage() : undefined
            }
        }
    }
}
