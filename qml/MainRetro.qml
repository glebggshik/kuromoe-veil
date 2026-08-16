import QtQuick
import AnimeClient 1.0
import QtQuick.Controls
import QtQuick.Window

// Точка входа для темы "Retro Terminal" — выбирается в main.cpp вместо
// Main.qml, если AppConfig::theme() == "retro". Тот же frameless-chrome
// (перетаскивание/ресайз за края), что у обычной темы. Данные — те же
// бриджи (CatalogBridge/DetailBridge/BookmarksBridge), что у обычной темы,
// просто другой QML-скин поверх; сплэш-скрин и auto-browse/auto-calendar
// аргументы тут пока не нужны — RetroShell грузит данные сам при старте.
ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: "KuroMoe Veil — Retro Terminal"
    color: RetroTheme.background
    flags: Qt.Window | Qt.FramelessWindowHint

    onClosing: function(close) {
        close.accepted = true
    }

    // "Фейковый" maximize — вручную растягиваем геометрию окна на весь экран
    // вместо native visibility = Window.Maximized. У фреймлесс-окна (без
    // Qt.FramelessWindowHint нативный maximize идёт через штатную анимацию
    // системы) на Windows нативная анимация разворота иногда не успевает
    // синхронно перелейаутить QML-контент — на кадр видно старый размер
    // контента поверх уже увеличенного чёрного фона окна. Ручная геометрия
    // разворачивает окно мгновенно, без системной анимации — кадра не будет.
    property bool fakeMaximized: false
    property rect restoreGeom: Qt.rect(x, y, width, height)

    function toggleMaximize() {
        if (root.fakeMaximized) {
            root.x = root.restoreGeom.x
            root.y = root.restoreGeom.y
            root.width = root.restoreGeom.width
            root.height = root.restoreGeom.height
            root.fakeMaximized = false
        } else {
            root.restoreGeom = Qt.rect(root.x, root.y, root.width, root.height)
            // Screen.desktopAvailableGeometry (attached-property синтаксис) тут
            // не резолвится изнутри обычной JS-функции — undefined. window.screen
            // (обычное свойство QQuickWindow) работает надёжно в любом контексте.
            root.x = root.screen.virtualX
            root.y = root.screen.virtualY
            root.width = root.screen.desktopAvailableWidth
            root.height = root.screen.desktopAvailableHeight
            root.fakeMaximized = true
        }
    }

    RetroShell {
        id: shell
        anchors.fill: parent
        appWindow: root
    }

    // Для --auto-calendar (main.cpp) и отладки: открыть SCHED / календарь.
    function openBrowseCalendar() {
        if (shell)
            shell.navigate("schedule")
    }

    property bool splashVisible: true

    // active: false после dismiss() полностью выгружает RetroSplashScreen
    // (не просто visible: false) — иначе его дочерние анимации (мигающий
    // курсор и т.п.) продолжали бы крутиться в фоне вечно и держали бы
    // рендер-луп окна в состоянии непрерывной перерисовки (баг с миганием
    // всего окна раз в секунду, см. комментарий в RetroSplashScreen.qml).
    Loader {
        id: splashLoader
        anchors.fill: parent
        z: 100
        active: root.splashVisible
        sourceComponent: RetroSplashScreen {
            onDismissed: root.splashVisible = false
        }
    }

    // Фиксированная минимальная задержка — в отличие от обычной темы тут
    // нет единого "splashDataReady" (RetroShell грузит разделы лениво по
    // мере навигации), поэтому просто ждём, пока проиграется boot-анимация.
    Timer {
        interval: 1700
        running: root.splashVisible
        onTriggered: {
            const item = splashLoader.item
            if (item)
                item.dismiss()
            else
                root.splashVisible = false
        }
    }

    // Ресайз окна за края — так же, как в Main.qml.
    Item {
        anchors.fill: parent
        visible: root.visibility === Window.Windowed && !root.fakeMaximized
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
}
