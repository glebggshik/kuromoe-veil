import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "components"

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: "KuroMoe Veil"
    color: Theme.bgApp
    flags: Qt.Window | Qt.FramelessWindowHint

    onClosing: function(close) {
        close.accepted = true
    }

    property int currentIndex: 0
    // Вперёд после pop() из DetailView — иначе мышь листает пагинацию каталога
    property var _navForward: []

    property bool splashVisible: true
    property bool splashMinTimeDone: false
    property bool splashDataReady: false

    readonly property bool cinemaActive: stackView.currentItem && stackView.currentItem.cinemaMode === true
    readonly property bool showTitleBar: !root.cinemaActive
    readonly property bool showChrome: !root.splashVisible && !root.cinemaActive && stackView.depth <= 1

    function tryDismissSplash() {
        if (root.splashMinTimeDone && root.splashDataReady && root.splashVisible)
            splash.dismiss()
    }

    onCinemaActiveChanged: {
        if (root.cinemaActive)
            root.visibility = Window.FullScreen
        else
            root.visibility = Window.Windowed
    }

    function clearNavForward() {
        root._navForward = []
    }

    function tabHostItem() {
        return stackView.depth >= 1 ? stackView.get(0) : null
    }

    function activeCatalogView() {
        var host = root.tabHostItem()
        if (!host || !host.activeTabView)
            return null
        return host.activeTabView
    }

    function openDetail(item) {
        root.clearNavForward()
        stackView.push(detailComponent, { item: item })
    }

    function switchTab(index) {
        if (root.currentIndex === index && stackView.depth === 1)
            return
        root.clearNavForward()
        while (stackView.depth > 1)
            stackView.pop(StackView.Immediate)
        root.currentIndex = index
        if (index === 2) {
            var host = root.tabHostItem()
            if (host && host.bookmarksView)
                host.bookmarksView.ensureLoaded()
        }
    }

    function openBrowseCalendar() {
        root.switchTab(1)
        Qt.callLater(function() {
            var host = root.tabHostItem()
            if (host && host.browseView)
                host.browseView.selectTab("calendar")
        })
    }

    function reloadBookmarks() {
        var host = root.tabHostItem()
        if (host && host.bookmarksView)
            host.bookmarksView.reload()
    }

    function openGenreFilter(genreId) {
        root.clearNavForward()
        while (stackView.depth > 1)
            stackView.pop(StackView.Immediate)
        root.currentIndex = 0
        var host = root.tabHostItem()
        if (!host || !host.homeView)
            return
        host.homeView.initialGenreId = genreId
        host.homeView.initialYear = ""
        host.homeView.filtersOpen = true
        host.homeView.filterGenres = [genreId]
        host.homeView.applyFilters()
    }

    function openYearFilter(year) {
        root.clearNavForward()
        while (stackView.depth > 1)
            stackView.pop(StackView.Immediate)
        root.currentIndex = 0
        var host = root.tabHostItem()
        if (!host || !host.homeView)
            return
        host.homeView.initialYear = String(year)
        host.homeView.initialGenreId = 0
        host.homeView.filtersOpen = true
        host.homeView.filterYear = String(year)
        host.homeView.applyFilters()
    }

    function mouseGoBack() {
        if (stackView.depth > 1) {
            var cur = stackView.currentItem
            if (cur && cur.item !== undefined)
                root._navForward.push({ type: "detail", item: cur.item })
            stackView.pop()
            return
        }
        root.clearNavForward()
        if (root.currentIndex === 0) {
            var host = root.tabHostItem()
            if (host && host.homeView && host.homeView.handleBack())
                return
        }
        var catalog = root.activeCatalogView()
        if (root.pagingAvailable && catalog && catalog.hasPrev)
            catalog.goPrevPage()
    }

    function mouseGoForward() {
        if (root._navForward.length > 0) {
            var entry = root._navForward.pop()
            if (entry.type === "detail" && entry.item)
                stackView.push(detailComponent, { item: entry.item })
            return
        }
        var catalog = root.activeCatalogView()
        if (root.pagingAvailable && catalog && catalog.hasNext)
            catalog.goNextPage()
    }

    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.BackButton)
                root.mouseGoBack()
            else if (mouse.button === Qt.ForwardButton)
                root.mouseGoForward()
        }
    }

    // Escape — тот же "назад", что и мышиная кнопка Back: закрывает
    // поиск/фильтры на Главной (через homeView.handleBack()), либо уходит
    // с экрана деталей/на предыдущую страницу пагинации. Shortcut ловит
    // клавишу глобально, независимо от того, что сейчас в фокусе.
    Shortcut {
        sequence: "Escape"
        onActivated: root.mouseGoBack()
    }

    TitleBar {
        id: titleBar
        window: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: root.showTitleBar
    }

    // Ресайз по краям — у frameless-окна нет системной рамки.
    Item {
        id: resizeLayer
        anchors.fill: parent
        visible: root.showTitleBar && root.visibility === Window.Windowed
        z: 25

        component ResizeEdge : MouseArea {
            property int edge: 0
            cursorShape: Qt.ArrowCursor
            onPressed: function(mouse) {
                if (mouse.button === Qt.LeftButton && root.startSystemResize)
                    root.startSystemResize(edge)
            }
        }

        ResizeEdge { edge: Qt.LeftEdge;   anchors.left: parent.left;   anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; cursorShape: Qt.SizeHorCursor }
        ResizeEdge { edge: Qt.RightEdge;  anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; cursorShape: Qt.SizeHorCursor }
        ResizeEdge { edge: Qt.TopEdge;    anchors.top: parent.top;    anchors.left: parent.left; anchors.right: parent.right; height: 5; cursorShape: Qt.SizeVerCursor }
        ResizeEdge { edge: Qt.BottomEdge; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 5; cursorShape: Qt.SizeVerCursor }
    }

    SplashScreen {
        id: splash
        anchors.fill: parent
        visible: root.splashVisible
        onDismissed: root.splashVisible = false
    }

    Timer {
        id: splashMinTimer
        interval: 1400
        repeat: false
        running: root.splashVisible
        onTriggered: {
            root.splashMinTimeDone = true
            root.tryDismissSplash()
        }
    }

    Timer {
        id: splashMaxTimer
        interval: 9000
        repeat: false
        running: root.splashVisible
        onTriggered: {
            root.splashMinTimeDone = true
            root.splashDataReady = true
            root.tryDismissSplash()
        }
    }

    StackView {
        id: stackView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.showTitleBar ? titleBar.bottom : parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.showChrome ? bottomNav.height + bottomNav.anchors.bottomMargin + 8 : 0
        initialItem: tabHostComponent

        Behavior on anchors.topMargin { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on anchors.bottomMargin { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    }

    // Вкладки живут в одном контейнере — не пересоздаём их через replace
    // (иначе ломается направление анимации и возможны краши при частом клике).
    Component {
        id: tabHostComponent
        Item {
            id: tabHost
            clip: true

            property alias homeView: homeView
            property alias browseView: browseView
            property alias bookmarksView: bookmarksView
            readonly property var activeTabView: root.currentIndex === 0 ? homeView
                : (root.currentIndex === 1 ? browseView : null)

            readonly property bool preloadHomeDone: homeView.catalogReady && homeView.heroReady
            readonly property bool preloadBrowseDone: browseView.dataReady
            readonly property bool preloadBookmarksDone: bookmarksView.dataReady

            function checkPreload() {
                if (preloadHomeDone && preloadBrowseDone && preloadBookmarksDone) {
                    root.splashDataReady = true
                    root.tryDismissSplash()
                }
            }

            Connections {
                target: homeView
                function onCatalogReadyChanged() { tabHost.checkPreload() }
                function onHeroReadyChanged() { tabHost.checkPreload() }
            }
            Connections {
                target: browseView
                function onDataReadyChanged() { tabHost.checkPreload() }
            }
            Connections {
                target: bookmarksView
                function onDataReadyChanged() { tabHost.checkPreload() }
            }

            Component.onCompleted: {
                browseView.ensureLoaded()
                bookmarksView.ensureLoaded()
                tabHost.checkPreload()
            }

            Item {
                id: tabStrip
                width: tabHost.width * 4
                height: parent.height
                x: -root.currentIndex * tabHost.width

                Behavior on x {
                    enabled: stackView.depth === 1
                    NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                }

                HomeView {
                    id: homeView
                    width: tabHost.width
                    height: tabHost.height
                    x: 0
                    tabActive: root.currentIndex === 0
                    windowExpanded: root.visibility === Window.Maximized
                        || root.visibility === Window.FullScreen
                    onOpenDetail: function(item) { root.openDetail(item) }
                }

                BrowseView {
                    id: browseView
                    width: tabHost.width
                    height: tabHost.height
                    x: tabHost.width
                    tabActive: root.currentIndex === 1
                    onOpenDetail: function(item) { root.openDetail(item) }
                }

                BookmarksView {
                    id: bookmarksView
                    width: tabHost.width
                    height: tabHost.height
                    x: tabHost.width * 2
                    tabActive: root.currentIndex === 2
                    onOpenDetail: function(item) { root.openDetail(item) }
                }

                SettingsView {
                    width: tabHost.width
                    height: tabHost.height
                    x: tabHost.width * 3
                    embedded: true
                }
            }
        }
    }

    Component {
        id: detailComponent
        DetailView {
            onBackRequested: root.mouseGoBack()
            onOpenDetail: function(item) { root.openDetail(item) }
            onGenreClicked: function(genreId) {
                stackView.pop()
                root.openGenreFilter(genreId)
            }
            onYearClicked: function(year) {
                stackView.pop()
                root.openYearFilter(year)
            }
        }
    }

    readonly property var catalogPagerView: {
        if (!root.showChrome || stackView.depth !== 1)
            return null
        var host = stackView.get(0)
        return host ? host.activeTabView : null
    }

    readonly property bool pagingAvailable: catalogPagerView
        && catalogPagerView.hasPrev !== undefined

    Rectangle {
        id: bottomNav
        height: 48
        radius: 24
        color: "#1C1C1E"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        visible: root.showChrome
        opacity: visible ? 1 : 0
        z: 20
        width: navRow.width + 20

        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Row {
            id: navRow
            anchors.centerIn: parent
            spacing: 2

            NavButton {
                iconSize: 20
                iconSource: Qt.resolvedUrl("assets/home.svg")
                isActive: root.currentIndex === 0 && stackView.depth === 1
                onClicked: root.switchTab(0)
            }

            NavButton {
                iconSize: 20
                iconSource: Qt.resolvedUrl("assets/compass.svg")
                isActive: root.currentIndex === 1 && stackView.depth === 1
                onClicked: root.switchTab(1)
            }

            NavButton {
                iconSize: 20
                iconSource: Qt.resolvedUrl("assets/bookmark.svg")
                isActive: root.currentIndex === 2 && stackView.depth === 1
                onClicked: root.switchTab(2)
            }

            NavButton {
                iconSize: 20
                iconSource: Qt.resolvedUrl("assets/settings.svg")
                isActive: root.currentIndex === 3 && stackView.depth === 1
                onClicked: root.switchTab(3)
            }
        }
    }
}