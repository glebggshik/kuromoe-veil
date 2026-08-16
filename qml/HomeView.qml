import QtQuick
import QtQuick.Controls
import AnimeClient 1.0
import "components"

Item {
    id: root
    property bool tabActive: true
    property bool catalogReady: false
    property bool heroReady: false
    // Минимум — компактное окно (~1280). Выше растёт с шириной и в развёрнутом окне.
    property bool windowExpanded: false
    readonly property int heroHeightMin: 310
    readonly property int heroHeightMax: 400
    readonly property int heroHeight: {
        var minH = root.heroHeightMin
        var maxH = root.heroHeightMax
        var w = Math.max(root.width, 1)
        var t = Math.min(1, Math.max(0, (w - 1280) / (1920 - 1280)))
        if (root.windowExpanded)
            t = Math.min(1, Math.max(t, 0.75))
        return Math.round(minH + t * (maxH - minH))
    }
    property var cardsModel: []
    signal openDetail(var item)

    // заполняются снаружи (Main.qml), когда сюда переходят по клику на тег
    // жанра/года с карточки на другом экране — тогда сразу открываем
    // фильтрованный список вместо обычного "Популярное"
    property int initialGenreId: 0
    property string initialYear: ""

    // единая "контентная" ширина — под неё выравнивается и сетка карточек,
    // и поиск/пилюли, и пагинация, чтобы все блоки стояли на одном уровне,
    // а не были растянуты each-on-its-own на всю ширину окна
    readonly property int slot: 166
    readonly property int gridColumns: Math.max(1, Math.floor((width - 32) / slot))
    // GridView: N колонок = N * slot (в Flow было N*160 + (N-1)*6 = N*slot - 6)
    readonly property int contentWidth: Math.min(width - 32, gridColumns * slot)

    property bool filtersOpen: false
    property bool searchFocused: false
    property bool searchActive: false
    property bool suggestLoading: false
    property var searchSuggestions: []
    property string filterKind: ""
    property var filterGenres: []
    property string filterYear: ""

    readonly property bool showSuggestions: root.searchFocused
        && searchField.text.trim().length >= 2
        && root.searchSuggestions.length > 0
    readonly property bool headerPinned: root.filtersOpen || root.searchFocused || root.showSuggestions

    readonly property var kindOptions: [
        { value: "tv", label: "ТВ" },
        { value: "movie", label: "Фильм" },
        { value: "ova", label: "OVA" },
        { value: "ona", label: "ONA" },
        { value: "special", label: "Спешл" },
        { value: "music", label: "Клип" }
    ]
    // популярные жанры — показаны сразу; полный список Шикимори (включая
    // редкие типа "Авангард") — за "Ещё жанры" ниже
    readonly property var popularGenreIds: [1, 2, 4, 8, 10, 12, 14, 22, 24, 30, 36, 37, 7, 117, 18, 130, 38, 40, 35, 23, 19, 27, 42]
    property var allGenreOptions: []
    property bool showAllGenres: false

    readonly property var genreOptions: root.allGenreOptions.filter(function(g) { return root.popularGenreIds.indexOf(g.id) >= 0 })

    // если выбран жанр не из популярных (например, кликнули тег "Авангард"
    // на карточке) — на время этого поиска показываем его первым тегом,
    // иначе непонятно, что и почему выделено активным
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

    function toggleGenre(id) {
        var arr = root.filterGenres.slice()
        var idx = arr.indexOf(id)
        if (idx >= 0) arr.splice(idx, 1)
        else arr.push(id)
        root.filterGenres = arr
    }

    function applyFilters() {
        root.searchActive = false
        root.searchSuggestions = []
        searchField.text = ""
        bridge.applyFilters(root.filterKind, root.filterGenres, root.filterYear)
    }

    function resetFilters() {
        root.filterKind = ""
        root.filterGenres = []
        root.filterYear = ""
        root.scrollToTopOnResults = true
        bridge.loadOngoing()
    }

    function commitSearch(query) {
        var q = String(query || searchField.text).trim()
        if (q.length === 0)
            return
        root.searchSuggestions = []
        root.suggestLoading = false
        root.searchActive = true
        root.filtersOpen = true
        searchField.focus = false
        root.scrollToTopOnResults = true
        bridge.search(q)
    }

    function returnToHomeFeed() {
        root.searchActive = false
        root.searchSuggestions = []
        root.suggestLoading = false
        searchField.text = ""
        searchField.focus = false
        root.filtersOpen = false
        root.filterKind = ""
        root.filterGenres = []
        root.filterYear = ""
        root.scrollToTopOnResults = true
        bridge.loadOngoing()
    }

    function handleBack() {
        if (root.searchActive || searchField.text.trim().length > 0) {
            root.returnToHomeFeed()
            return true
        }
        if (root.filtersOpen) {
            if (root.filterKind || root.filterGenres.length || root.filterYear) {
                root.resetFilters()
                root.filtersOpen = false
            } else {
                root.filtersOpen = false
                searchField.focus = false
            }
            return true
        }
        return false
    }

    property bool scrollToTopOnResults: false

    readonly property bool hasPrev: bridge.hasPrev
    readonly property bool hasNext: bridge.hasNext

    function goPrevPage() {
        root.scrollToTopOnResults = true
        bridge.prevPage()
    }

    function goNextPage() {
        root.scrollToTopOnResults = true
        bridge.nextPage()
    }

    Timer {
        id: scrollToTopTimer
        interval: 200
        onTriggered: flick.contentY = 0
    }

    Timer {
        id: suggestTimer
        interval: 280
        onTriggered: {
            root.suggestLoading = true
            bridge.searchSuggestions(searchField.text)
        }
    }

    Connections {
        target: historyManager
        function onCurrentChanged(titleId, episode) {
            if (titleId)
                bridge.loadHero()
        }
    }

    CatalogBridge {
        id: bridge
        onResultsReady: function(items) {
            // Qt.callLater перед catalogReady — см. комментарий в BrowseView.qml:
            // сплэш должен ждать реального layout делегатов, а не только
            // присвоения модели (иначе фриз создания карточек виден пользователю
            // на первом кадре после сплэша вместо того, чтобы быть скрытым им).
            root.cardsModel = items
            posterCache.preloadCatalog(items)
            Qt.callLater(function() { root.catalogReady = true })
            if (root.scrollToTopOnResults) {
                root.scrollToTopOnResults = false
                scrollToTopTimer.start()
            }
        }
        onHeroReady: function(item) {
            root.heroReady = true
            hero.setData(item)
        }
        onSearchSuggestionsReady: function(items) {
            root.suggestLoading = false
            root.searchSuggestions = items || []
        }
        Component.onCompleted: {
            // Мгновенно показываем закэшированный hero, ПОКА идёт сетевой
            // запрос (Shikimori/AniList, секунда-две). Но снимок берём только
            // если он соответствует тому тайтлу, который loadHero() и так
            // сейчас разрешит — т.е. самому свежему в БД истории (она —
            // единственный авторитетный источник "что смотрели последним").
            // Иначе при рестарте/двух запущенных копиях мелькал чужой/старый
            // кадр: snapshot в config.ini перезаписывался другой копией, а БД
            // оставалась общей. Сверка по id убирает рассинхрон.
            var recent = historyManager.mostRecent()
            if (!recent || !recent.found || !recent.titleId)
                return
            var cached = appConfig.lastHero()
            if (cached && cached.title && String(cached.id) === String(recent.titleId)) {
                // Снимок мог сохранить устаревший heroImageLocal (постер) — резолвим из heroBanner.
                cached = Object.assign({}, cached)
                delete cached.heroImageLocal
                hero.setData(cached)
            }
        }
        onError: function(msg) {
            root.catalogReady = true
            statusLabel.text = "Ошибка: " + msg
        }
    }

    Component.onCompleted: {
        bridge.loadHero()
        root.allGenreOptions = bridge.allGenres()
        if (root.initialGenreId > 0) {
            root.filterGenres = [root.initialGenreId]
            root.filtersOpen = true
            root.applyFilters()
        } else if (root.initialYear) {
            root.filterYear = root.initialYear
            root.filtersOpen = true
            root.applyFilters()
        } else {
            bridge.loadOngoing()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentColumn
            width: flick.width

            HeroBanner {
                id: hero
                width: parent.width
                height: root.heroHeight
                imagesEnabled: true
                onPlayClicked: function(item) { root.openDetail(item) }
            }

            // Резерв под sticky-поиск (оверлей header), чтобы карточки не уезжали под него
            Item { width: 1; height: header.height }

            Item { width: 1; height: 16 }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                // Заголовок секции вместо мелкой техничной подписи "N онгоингов" —
                // виден только в базовом состоянии; во время поиска/фильтра
                // актуальнее счётчик результатов (он остался в statusLabel в шапке).
                visible: !root.searchActive && !root.filterKind
                    && root.filterGenres.length === 0 && !root.filterYear
                text: "Новинки"
                color: Theme.textPrimary
                font.pixelSize: 24
                font.bold: true
                topPadding: 4
                bottomPadding: 14
            }

            GridView {
                id: cardGrid
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
                    imagesEnabled: root.tabActive
                    showAiredEpisode: !root.searchActive
                        && !root.filterKind
                        && root.filterGenres.length === 0
                        && !root.filterYear
                    onClicked: root.openDetail(modelData)
                    onGenreClicked: function(genreId) {
                        root.filtersOpen = true
                        root.filterGenres = [genreId]
                        root.scrollToTopOnResults = true
                        root.applyFilters()
                    }
                    onYearClicked: function(year) {
                        root.filtersOpen = true
                        root.filterYear = String(year)
                        root.scrollToTopOnResults = true
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

    // При открытых фильтрах/поиске header (ниже) перекрывает только свою
    // собственную высоту (поиск + панель фильтров) — герой-баннер под ним
    // выше этой высоты, и его нижняя часть (картинка/текст) торчала из-под
    // шапки и просвечивала сквозь пустое место. Сплошной фон на весь экран
    // ниже шапки полностью прячет контент позади, пока открыты фильтры.
    Rectangle {
        anchors.fill: parent
        color: Theme.bgApp
        z: 9
        visible: root.headerPinned
    }

    // === Sticky-заголовок: поиск + пилюли ===
    // В Tkinter это требовало ручного polling позиции скролла каждые 50мс.
    // В QML — просто реактивный биндинг на contentY, Qt сам перерисовывает
    // плавно на каждый кадр скролла.
    Rectangle {
        id: header
        width: parent.width
        height: headerColumn.height
        color: Theme.bgApp
        z: 10
        // при открытых фильтрах заголовок сразу приклеен сверху (не ждёт
        // прокрутки мимо хиро) — иначе на маленьком окне с развёрнутой
        // панелью фильтров под карточки оставалось всего пара строк
        y: root.headerPinned ? 0 : Math.max(0, root.heroHeight - flick.contentY)

        Column {
            id: headerColumn
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.contentWidth

            Row {
                width: parent.width
                topPadding: 20
                bottomPadding: root.showSuggestions ? 4 : 10

                Rectangle {
                    width: parent.width
                    height: 40
                    radius: Theme.cornerPill
                    color: Theme.bgInput

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 12
                        spacing: 6
                        Image {
                            width: 16
                            height: 16
                            source: Qt.resolvedUrl("assets/search.svg")
                            sourceSize: Qt.size(32, 32)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            opacity: 0.45
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TextField {
                            id: searchField
                            width: parent.width - 22
                            anchors.verticalCenter: parent.verticalCenter
                            placeholderText: "Поиск аниме..."
                            color: Theme.textPrimary
                            background: Item {}
                            onFocusChanged: {
                                root.searchFocused = focus
                                if (focus)
                                    root.filtersOpen = true
                            }
                            onTextChanged: {
                                root.searchActive = false
                                if (text.trim().length < 2) {
                                    root.searchSuggestions = []
                                    root.suggestLoading = false
                                    suggestTimer.stop()
                                    return
                                }
                                suggestTimer.restart()
                            }
                            onAccepted: root.commitSearch(text)
                        }
                    }
                }
            }

            Item {
                width: parent.width
                implicitHeight: root.filtersOpen ? filtersPanel.height : 0

                Column {
                    id: filtersPanel
                    width: parent.width
                    visible: root.filtersOpen
                    spacing: 10
                    bottomPadding: 14
                    opacity: root.showSuggestions ? 0.35 : 1.0

                    Behavior on opacity { NumberAnimation { duration: 120 } }

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
                    Text { text: "Год"; color: Theme.textMuted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle {
                        width: 90; height: 32
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
                    PillButton { text: "Сбросить"; onClicked: root.resetFilters() }
                }
                }

                Rectangle {
                    width: parent.width
                    visible: root.showSuggestions
                    anchors.top: parent.top
                    z: 2
                    radius: Theme.cornerSmall
                    color: Theme.bgCard
                    border.width: 1
                    border.color: "#33ffffff"
                    implicitHeight: suggestColumn.height + 2

                    Column {
                        id: suggestColumn
                        width: parent.width - 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 0

                        Repeater {
                            model: Math.min(3, root.searchSuggestions.length)
                            delegate: Rectangle {
                                width: parent.width
                                height: 44
                                color: suggestMouse.containsMouse ? Theme.bgCardHover : "transparent"

                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    text: root.searchSuggestions[index].title || ""
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }

                                MouseArea {
                                    id: suggestMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var item = root.searchSuggestions[index]
                                        searchField.text = item.title || ""
                                        root.commitSearch(searchField.text)
                                    }
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: "#18ffffff"
                                    visible: index < Math.min(3, root.searchSuggestions.length) - 1
                                }
                            }
                        }
                    }
                }
            }

            Text {
                id: statusLabel
                bottomPadding: 6
                // Счётчик нужен только когда пользователь реально ищет/фильтрует —
                // в базовом состоянии секцию озаглавливает крупный "Новинки" в
                // основном контенте, техничная подпись "N онгоингов" тут не нужна.
                visible: bridge.loading || root.searchActive
                    || !!root.filterKind || root.filterGenres.length > 0 || !!root.filterYear
                text: {
                    if (bridge.loading)
                        return "Загрузка..."
                    if (root.searchActive)
                        return root.cardsModel.length + " результатов"
                    return root.cardsModel.length + " тайтлов"
                }
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }
    }

}
