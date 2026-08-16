import QtQuick
import AnimeClient 1.0
import QtQuick.Window

// Порт desktop-shell.tsx — общая оболочка со всеми разделами.
// В отличие от оригинала: на экране деталей sidebar НЕ подсвечивает "Home"
// (в оригинале navActive = screen === 'detail' ? 'home' : screen — заметная
// нестыковка на скриншотах, тут сознательно не повторяем).
Item {
    id: root

    // Не required — песочница (qml.exe без окна снаружи) должна продолжать
    // работать без параметра; в реальном приложении прокидывается ApplicationWindow.
    property Window appWindow: null

    property string screen: "home"          // home | browse | bookmarks | settings | detail
    // Целый объект карточки (id/title/poster/...), не просто id — DetailBridge.load()
    // принимает весь item, как и у обычной DetailView.qml.
    property var selectedItem: ({})

    // Состояние фильтров Browse, вынесенное сюда — Loader ниже пересоздаёт
    // RetroBrowse при каждой навигации (уходишь на Detail — возвращаешься на
    // Browse), и без этого фильтры/вкладка сбрасывались в дефолт при каждом
    // возврате. RetroBrowse при создании стартует с этих значений и пишет
    // сюда же при каждом изменении (см. browseComp).
    property string savedBrowseTab: "popular"
    property string savedFilterKind: ""
    property var savedFilterGenres: []
    property string savedFilterYear: ""

    readonly property var paths: ({
        home: "~/home", browse: "~/browse", schedule: "~/cron", bookmarks: "~/library",
        settings: "~/config", detail: "~/watch"
    })
    // На detail ничего не подсвечиваем в sidebar (в оригинале был баг: подсвечивался Home)
    readonly property string sidebarActive: root.screen === "detail" ? "" : root.screen

    // Полноэкранный плеер (cinemaMode у RetroDetail) должен закрывать ВСЁ
    // окно, а не только область справа от сайдбара — иначе тайтлбар и
    // сайдбар остаются поверх видео даже когда окно уже свёрнуто в
    // Window.FullScreen (баг: "видео растягивается, но шапка/сайдбар
    // видны"). Прячем их отсюда же, на уровне шелла.
    readonly property bool cinemaActive: screenLoader.item && screenLoader.item.cinemaMode === true

    // История навигации — как в браузере: кнопки "назад"/"вперёд" на мыши
    // (XButton1/XButton2, обычно кнопки 4/5 сбоку — "8"/"9" в терминологии
    // вопроса) листают её. history — плоский список {screen, item},
    // historyIndex — текущая позиция; переход НЕ через back/forward обрезает
    // всё, что было "вперёди" (как в реальном браузере).
    property var history: [{ screen: "home", item: ({}) }]
    property int historyIndex: 0

    function pushHistory(screen, item) {
        root.history = root.history.slice(0, root.historyIndex + 1)
        root.history.push({ screen: screen, item: item || ({}) })
        root.historyIndex = root.history.length - 1
    }

    function navigate(s) {
        // "random" — не экран, а действие: подобрать случайный тайтл и сразу
        // открыть его детали. Отдельная кнопка в сайдбаре, но не должна
        // становиться текущим root.screen (нечего подсвечивать/на что
        // возвращаться по "назад").
        if (s === "random") {
            randomBridge.pickRandom()
            return
        }
        if (root.screen === s)
            return
        root.screen = s
        root.pushHistory(s, root.selectedItem)
    }
    function openAnime(item) {
        root.selectedItem = item
        root.screen = "detail"
        root.pushHistory("detail", item)
    }
    function goBack() {
        if (root.historyIndex <= 0)
            return
        root.historyIndex -= 1
        const entry = root.history[root.historyIndex]
        root.selectedItem = entry.item
        root.screen = entry.screen
    }
    function goForward() {
        if (root.historyIndex >= root.history.length - 1)
            return
        root.historyIndex += 1
        const entry = root.history[root.historyIndex]
        root.selectedItem = entry.item
        root.screen = entry.screen
    }

    // Для кнопки RANDOM в сайдбаре — отдельный лёгкий бридж уровня шелла,
    // а не завязка на бридж какого-то из экранов (тот может быть не создан
    // прямо сейчас, если пользователь на Settings/Bookmarks).
    CatalogBridge {
        id: randomBridge
        onRandomReady: function(item) { if (item) root.openAnime(item) }
        onError: function(msg) { console.warn("Retro Shell (random):", msg) }
    }

    // Кнопки "назад"/"вперёд" сбоку мыши — MouseArea без acceptedButtons
    // для ЛКМ/ПКМ не перехватывает обычные клики (они проваливаются в
    // контент ниже), реагирует только на Back/Forward.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.BackButton)
                root.goBack()
            else if (mouse.button === Qt.ForwardButton)
                root.goForward()
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        RetroTitleBar {
            width: parent.width
            height: 36
            window: root.appWindow
            visible: !root.cinemaActive
        }

        // === Breadcrumb / статус-строка ===
        Rectangle {
            width: parent.width
            height: 28
            visible: !root.cinemaActive
            color: RetroTheme.card
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: RetroTheme.border }

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                spacing: 6
                Text { text: "user@terminal.watch"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.primary }
                Text { text: ":"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.mutedForeground }
                Text { text: root.paths[root.screen] || ""; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.accent }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 16
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 6
                    color: RetroTheme.primary
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 0.25; duration: 700 }
                        NumberAnimation { from: 0.25; to: 1; duration: 700 }
                    }
                }
                Text { text: "CONNECTED"; font.family: RetroTheme.fontFamily; font.pixelSize: 9; color: RetroTheme.mutedForeground }
            }
        }

        Row {
            width: parent.width
            height: root.cinemaActive ? parent.height : (parent.height - 36 - 28)
            spacing: 0

            RetroSidebar {
                height: parent.height
                active: root.sidebarActive
                visible: !root.cinemaActive
                onNavigate: function(s) { root.navigate(s) }
            }

            Item {
                width: root.cinemaActive ? parent.width : (parent.width - 68)
                height: parent.height

                Loader {
                    id: screenLoader
                    anchors.fill: parent
                    sourceComponent: {
                        switch (root.screen) {
                        case "browse": return browseComp
                        case "schedule": return scheduleComp
                        case "bookmarks": return bookmarksComp
                        case "settings": return settingsComp
                        case "detail": return detailComp
                        default: return homeComp
                        }
                    }
                }
            }
        }
    }

    Component {
        id: homeComp
        RetroHome { appWindow: root.appWindow; onOpenAnime: function(item) { root.openAnime(item) } }
    }
    Component {
        id: browseComp
        RetroBrowse {
            onOpenAnime: function(item) { root.openAnime(item) }
            currentTab: root.savedBrowseTab
            filterKind: root.savedFilterKind
            filterGenres: root.savedFilterGenres
            filterYear: root.savedFilterYear
            onCurrentTabChanged: root.savedBrowseTab = currentTab
            onFilterKindChanged: root.savedFilterKind = filterKind
            onFilterGenresChanged: root.savedFilterGenres = filterGenres
            onFilterYearChanged: root.savedFilterYear = filterYear
        }
    }
    Component {
        id: scheduleComp
        RetroSchedule { onOpenAnime: function(item) { root.openAnime(item) } }
    }
    Component {
        id: bookmarksComp
        RetroBookmarks { onOpenAnime: function(item) { root.openAnime(item) } }
    }
    Component {
        id: settingsComp
        RetroSettings {}
    }
    Component {
        id: detailComp
        RetroDetail {
            item: root.selectedItem
            appWindow: root.appWindow
            onOpenAnime: function(item) { root.openAnime(item) }
            onBackRequested: root.navigate("home")
        }
    }
}
