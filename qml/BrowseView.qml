import QtQuick
import QtQuick.Controls
import AnimeClient 1.0
import "components"

Item {
    id: root
    property bool tabActive: true
    signal openDetail(var item)

    readonly property var tabs: [
        { key: "popular", label: "Популярное", icon: "assets/flame.svg" },
        { key: "latest", label: "Последнее", icon: "assets/clock.svg" },
        { key: "calendar", label: "Расписание", icon: "assets/tv.svg" }
    ]
    property string currentTab: "popular"
    property var cardsModel: []
    // Фоновый прогрев декода постеров, пока пользователь ещё на другой вкладке
    // (тот же паттерн, что в BookmarksView) — без этого 50 картинок начинали
    // decode/paint одновременно ровно в момент первого переключения на
    // Обзор (posterActive false→true разом для всех делегатов), что и давало
    // заметный фриз даже когда сами данные каталога были готовы заранее.
    property bool warmPosters: false
    property var calendarDays: []
    property bool calendarLoading: false
    property bool calendarLoadDone: false
    property bool calendarHasSkeleton: false
    property int calendarRevision: 0
    property int calendarPosterEpoch: 0

    readonly property bool isCalendarTab: root.currentTab === "calendar"

    property bool filtersOpen: false
    property string filterKind: ""
    property var filterGenres: []
    property string filterYear: ""

    readonly property var kindOptions: [
        { value: "tv", label: "ТВ" },
        { value: "movie", label: "Фильм" },
        { value: "ova", label: "OVA" },
        { value: "ona", label: "ONA" },
        { value: "special", label: "Спешл" },
        { value: "music", label: "Клип" }
    ]
    readonly property var popularGenreIds: [1, 2, 4, 8, 10, 12, 14, 22, 24, 30, 36, 37, 7, 117, 18, 130, 38, 40, 35, 23, 19, 27, 42]
    property var allGenreOptions: []
    property bool showAllGenres: false

    readonly property var genreOptions: root.allGenreOptions.filter(function(g) {
        return root.popularGenreIds.indexOf(g.id) >= 0
    })

    readonly property var visibleGenreOptions: {
        var base = root.showAllGenres ? root.allGenreOptions : root.genreOptions
        var extraIds = root.filterGenres.filter(function(id) {
            return !base.some(function(g) { return g.id === id })
        })
        var extra = extraIds.map(function(id) {
            var found = root.allGenreOptions.find(function(g) { return g.id === id })
            return found || { id: id, name: "#" + id }
        })
        return extra.concat(base)
    }

    readonly property int slot: 166
    readonly property int gridColumns: Math.max(1, Math.floor((width - 32) / slot))
    readonly property int contentWidth: Math.min(width - 32, gridColumns * slot)

    property bool scrollToTopOnResults: false

    readonly property bool hasPrev: bridge.hasPrev
    readonly property bool hasNext: bridge.hasNext

    function toggleGenre(id) {
        var arr = root.filterGenres.slice()
        var idx = arr.indexOf(id)
        if (idx >= 0) arr.splice(idx, 1)
        else arr.push(id)
        root.filterGenres = arr
    }

    function applyFilters() {
        root.scrollToTopOnResults = true
        bridge.applyFilters(root.filterKind, root.filterGenres, root.filterYear)
    }

    function resetFilters() {
        root.filterKind = ""
        root.filterGenres = []
        root.filterYear = ""
        root.scrollToTopOnResults = true
        root.reload()
    }

    function goPrevPage() {
        root.scrollToTopOnResults = true
        bridge.prevPage()
    }

    function goNextPage() {
        root.scrollToTopOnResults = true
        bridge.nextPage()
    }

    function scrollActiveToTop() {
        if (root.isCalendarTab)
            calendarPane.listView.contentY = 0
        else if (catalogLoader.item)
            catalogLoader.item.contentY = 0
    }

    Timer {
        id: scrollToTopTimer
        interval: 200
        onTriggered: root.scrollActiveToTop()
    }

    CatalogBridge {
        id: bridge
        onResultsReady: function(items) {
            // Модель — ПЕРЕД dataReady: назначение cardsModel синхронно создаёт
            // ~50 GridView-делегатов (Card + PosterThumbnail) на UI-потоке.
            // Раньше dataReady выставлялся первой строкой — сплэш узнавал
            // "данные готовы" ДО этой тяжёлой работы и мог закрыться, пока
            // делегаты ещё только создаются — тяжесть переезжала на первый
            // реальный кадр после сплэша (виден как фриз при переходе на
            // вкладку). Плюс Qt.callLater — ждём, чтобы GridView реально
            // прогнал layout созданных делегатов, а не только присвоил модель.
            root.cardsModel = items
            posterCache.preloadCatalog(items)
            Qt.callLater(function() { root.dataReady = true })
            if (!root.tabActive)
                root.warmPosters = true
            if (root.scrollToTopOnResults) {
                root.scrollToTopOnResults = false
                scrollToTopTimer.start()
            }
        }
        onCalendarLoadStarted: {
            root.calendarLoading = true
            root.calendarHasSkeleton = false
        }
        onCalendarReady: function(days) {
            Qt.callLater(function() {
                if (!root.calendarHasSkeleton) {
                    root.calendarDays = days
                    root.calendarHasSkeleton = true
                    root.calendarRevision++
                } else {
                    if (root.patchCalendarPosters(days))
                        root.calendarPosterEpoch++
                }
                root.calendarLoading = false
            })
        }
        onRandomReady: function(item) { if (item) root.openDetail(item) }
        onError: function(msg) {
            root.dataReady = true
            statusLabel.text = "Ошибка: " + msg
            root.calendarLoading = false
        }
    }

    property bool initialLoadDone: false
    property bool dataReady: false

    function ensureLoaded() {
        if (root.initialLoadDone)
            return
        root.initialLoadDone = true
        root.allGenreOptions = bridge.allGenres()
        root.selectTab(root.currentTab)
    }

    onTabActiveChanged: {
        if (root.tabActive)
            root.warmPosters = false
    }

    function selectTab(key) {
        root.currentTab = key
        root.scrollToTopOnResults = true
        scrollToTopTimer.start()

        if (key === "calendar") {
            if (!root.calendarLoadDone) {
                root.calendarLoadDone = true
                bridge.loadCalendar()
            }
            return
        }

        root.reload()
    }

    function reload() {
        root.scrollToTopOnResults = true
        if (root.currentTab === "popular") bridge.loadPopular()
        else if (root.currentTab === "latest") bridge.loadLatest()
    }

    function findCalendarDay(days, dateKey) {
        for (var d = 0; d < days.length; ++d) {
            if (days[d].date === dateKey)
                return days[d]
        }
        return null
    }

    function findCalendarItem(items, id) {
        var sid = String(id)
        for (var i = 0; i < items.length; ++i) {
            if (String(items[i].id) === sid)
                return items[i]
        }
        return null
    }

    // GraphQL-enrich: обновляем poster-поля точечно (новый объект на тайтл), без сброса days[].
    function patchCalendarPosters(enrichedDays) {
        var anyChanged = false
        for (var d = 0; d < root.calendarDays.length; ++d) {
            var day = root.calendarDays[d]
            var srcDay = root.findCalendarDay(enrichedDays, day.date)
            if (!srcDay || !day.items || !srcDay.items)
                continue
            var items = day.items.slice()
            var changed = false
            for (var i = 0; i < items.length; ++i) {
                var item = items[i]
                var srcItem = root.findCalendarItem(srcDay.items, item.id)
                if (!srcItem)
                    continue
                var poster = srcItem.poster || item.poster || ""
                var posterHd = srcItem.posterHd || item.posterHd || ""
                if (poster === (item.poster || "") && posterHd === (item.posterHd || ""))
                    continue
                var copy = {}
                for (var k in item)
                    copy[k] = item[k]
                if (poster)
                    copy.poster = poster
                if (posterHd)
                    copy.posterHd = posterHd
                items[i] = copy
                changed = true
            }
            if (changed) {
                day.items = items
                anyChanged = true
            }
        }
        return anyChanged
    }

    Column {
        anchors.fill: parent
        spacing: 0

        Item {
            id: tabFlow
            anchors.horizontalCenter: parent.horizontalCenter
            width: tabRow.implicitWidth
            height: tabRow.implicitHeight + 26

            Row {
                id: tabRow
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 20
                spacing: 8

                Repeater {
                    model: root.tabs
                    delegate: PillButton {
                        text: modelData.label
                        iconSource: modelData.icon ? Qt.resolvedUrl(modelData.icon) : ""
                        active: root.currentTab === modelData.key
                        onClicked: root.selectTab(modelData.key)
                    }
                }

                PillButton {
                    text: "Рандом"
                    iconSource: Qt.resolvedUrl("assets/dice.svg")
                    onClicked: bridge.pickRandom()
                }
            }
        }

        Text {
            id: statusLabel
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.isCalendarTab
                ? (root.calendarLoading || root.calendarDays.length > 0)
                : true
            bottomPadding: 6
            text: {
                if (root.isCalendarTab) {
                    if (root.calendarLoading) return "Загрузка расписания..."
                    var n = 0
                    for (var i = 0; i < root.calendarDays.length; ++i) {
                        var items = root.calendarDays[i].items
                        n += items ? items.length : 0
                    }
                    return n + " тайтлов в расписании"
                }
                return bridge.loading ? "Загрузка..." : (root.cardsModel.length + " тайтлов")
            }
            color: Theme.textSecondary
            font.pixelSize: 12
        }

        Item {
            width: parent.width
            height: parent.height - tabFlow.height - statusLabel.height - 8

            CalendarSection {
                id: calendarPane
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                visible: root.isCalendarTab
                days: root.calendarDays
                revision: root.calendarRevision
                posterEpoch: root.calendarPosterEpoch
                loading: root.calendarLoading
                onOpenDetail: function(item) { root.openDetail(item) }
            }

            Text {
                anchors.centerIn: parent
                visible: root.isCalendarTab && !root.calendarLoading && root.calendarDays.length === 0
                text: "Нет данных расписания"
                color: Theme.textMuted
                font.pixelSize: 13
            }

            Loader {
                id: catalogLoader
                anchors.fill: parent
                active: !root.isCalendarTab
                sourceComponent: catalogFlickComponent
            }

            Component {
                id: catalogFlickComponent
                Flickable {
                id: catalogFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: catalogColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: catalogColumn
                    width: catalogFlick.width
                    spacing: 0

                    Column {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.contentWidth
                        topPadding: 4
                        spacing: 10

                        Row {
                            spacing: 8

                            PillButton {
                                text: "Фильтры" + (root.filterKind || root.filterGenres.length || root.filterYear ? " ●" : "")
                                iconSource: Qt.resolvedUrl("assets/filters.svg")
                                active: root.filtersOpen
                                onClicked: root.filtersOpen = !root.filtersOpen
                            }
                            PillButton {
                                visible: root.filterKind || root.filterGenres.length || root.filterYear
                                text: "Сбросить"
                                onClicked: root.resetFilters()
                            }
                        }

                        Column {
                            width: parent.width
                            visible: root.filtersOpen
                            spacing: 10
                            bottomPadding: 6

                            Text { text: "Формат"; color: Theme.textMuted; font.pixelSize: 11 }
                            Flow {
                                width: parent.width
                                spacing: 6
                                Repeater {
                                    model: root.kindOptions
                                    delegate: PillButton {
                                        text: modelData.label
                                        active: root.filterKind === modelData.value
                                        onClicked: root.filterKind = (root.filterKind === modelData.value ? "" : modelData.value)
                                    }
                                }
                            }

                            Text { text: "Жанры"; color: Theme.textMuted; font.pixelSize: 11 }
                            Flow {
                                width: parent.width
                                spacing: 6
                                Repeater {
                                    model: root.visibleGenreOptions
                                    delegate: PillButton {
                                        text: modelData.name
                                        active: root.filterGenres.indexOf(modelData.id) >= 0
                                        onClicked: root.toggleGenre(modelData.id)
                                    }
                                }
                                PillButton {
                                    text: root.showAllGenres ? "Скрыть" : "Ещё жанры"
                                    onClicked: root.showAllGenres = !root.showAllGenres
                                }
                            }

                            Row {
                                spacing: 10
                                Text {
                                    text: "Год"
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    width: 90
                                    height: 32
                                    radius: Theme.cornerSmall
                                    color: Theme.bgInput
                                    TextField {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        text: root.filterYear
                                        color: Theme.textPrimary
                                        background: Item {}
                                        placeholderText: "напр. 2016"
                                        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                                        onTextChanged: root.filterYear = text
                                    }
                                }
                                PillButton { text: "Применить"; active: true; onClicked: root.applyFilters() }
                            }
                        }
                    }

                    Item { width: 1; height: 8 }

                    GridView {
                        id: catalogGrid
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.contentWidth
                        height: {
                            var cols = Math.max(1, Math.floor(width / root.slot))
                            var rows = Math.ceil(count / cols)
                            return rows > 0 ? rows * (230 + 6) : 0
                        }
                        interactive: false
                        cellWidth: root.slot
                        cellHeight: 230 + 6
                        flow: GridView.FlowLeftToRight
                        model: root.cardsModel
                        cacheBuffer: 800
                        delegate: Card {
                            anime: modelData
                            imagesEnabled: (root.tabActive || root.warmPosters) && !root.isCalendarTab
                            onClicked: root.openDetail(modelData)
                            onGenreClicked: function(genreId) {
                                root.filtersOpen = true
                                root.filterGenres = [genreId]
                                root.applyFilters()
                            }
                            onYearClicked: function(year) {
                                root.filtersOpen = true
                                root.filterYear = String(year)
                                root.applyFilters()
                            }
                        }
                    }

                    PageNavRow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        page: bridge.page
                        hasPrev: bridge.hasPrev
                        hasNext: bridge.hasNext
                        onPrevClicked: root.goPrevPage()
                        onNextClicked: root.goNextPage()
                    }

                    Item { width: 1; height: 8 }
                }
                }
            }
        }
    }
}