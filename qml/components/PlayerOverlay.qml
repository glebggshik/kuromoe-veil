import QtQuick
import QtQuick.Controls

// uosc-style overlay: UI парит поверх видео, без блочных панелей.
Item {
    id: root

    required property var player
    property var playback: null
    property string titleText: ""
    property int maxEpisode: 0

    // Тема-параметры: обе темы (classic/retro) используют ОДИН хром и только
    // скинят цвета/иконки. Постер — на время loading/смены серии.
    property string posterSource: ""
    property string fontFamily: ""
    property bool showEpisodeList: true
    property bool showSkipOpening: false
    // Ретро: плеер встроен в прокручиваемую страницу — вне cinema колесо
    // должно скроллить страницу, а не крутить громкость.
    property bool wheelPassThroughOutsideCinema: false
    // Для кнопки «SAVE» в попапе задержки звука (audioDelay) — id тайтла
    // из темы; пустой — кнопка сохранения скрыта.
    property string audioSyncTitleId: ""

    readonly property bool active: player && player.hasMedia
    property bool cinemaMode: false
    property bool episodeListOpen: false

    readonly property string headerTitle: {
        var t = root.titleText || ""
        var ep = root.playback ? root.playback.currentEpisode : 0
        if (!t && ep > 0)
            return "Серия " + ep
        if (t && ep > 0)
            return t + " · Серия " + ep
        return t
    }

    readonly property string timeLineText: {
        var p = root.formatTime(player.position, false)
        var r = "-" + root.formatTime(Math.max(0, player.duration - player.position), false)
        return p + "  " + r
    }

    property bool audioMenuOpen: false
    property bool subtitleMenuOpen: false
    property bool delayPanelOpen: false

    // Для детекции двойного клика по mpv-правилам (время + расстояние курсора).
    property point lastClickPos: Qt.point(-10000, -10000)
    property real lastClickTime: 0

    readonly property bool panelsEngaged: root.episodeListOpen || root.audioMenuOpen || root.subtitleMenuOpen || root.delayPanelOpen || root.uiPinned

    readonly property int effectiveMaxEpisode: root.maxEpisode > 0
        ? root.maxEpisode
        : (root.playback ? root.playback.totalEpisodes : 0)

    signal requestEpisode(int episode)
    signal requestCinemaToggle()

    readonly property int fadeDuration: 300
    readonly property int hideDelayMs: 480
    readonly property int smoothMaxTime: 380
    readonly property real smoothVelocity: 2.4

    readonly property int topBarH: 44
    readonly property int controlsH: 36
    readonly property int volumeW: 52
    readonly property int timelineHitH: 40
    readonly property int bottomChromeH: 140

    readonly property int zoneStickPx: 10
    readonly property int bottomZoneH: root.bottomChromeH
    readonly property int topIn: 22
    readonly property int topOut: 72
    readonly property int bottomIn: root.bottomZoneH - 12
    readonly property int bottomOut: root.bottomZoneH + 24
    readonly property int volumeIn: 40
    readonly property int volumeOut: 100
    readonly property int borderRadius: 4

    // Палитра uosc: белый/серый, полупрозрачность (темы могут переопределить)
    property color fg: "#ffffff"
    property color fgMuted: "#ffffff99"
    property color trackBg: "#b3000000"
    property color trackFill: "#ffffff"
    property color shadeTop: "#b3000000"
    property color shadeBottom: "#b3000000"

    property bool uiPinned: false
    property bool cursorHidden: false
    property real mouseX: -10000
    property real mouseY: -10000
    property bool mouseInside: false

    property real topBarOpacity: 0
    property real bottomOpacity: 0
    property real volumeOpacity: 0

    property bool stickyTop: false
    property bool stickyBottom: false
    property bool stickyVolume: false
    property bool stickyEpisodeList: false

    readonly property real uiOpacity: root.uiPinned
        ? 1
        : Math.max(topBarOpacity, bottomOpacity, volumeOpacity)

    function playerIcon(name) {
        return Qt.resolvedUrl("../assets/player/" + name)
    }

    function showCursor() {
        root.cursorHidden = false
    }

    function hideCursorNow() {
        if (!root.active || root.uiPinned || root.audioMenuOpen || root.subtitleMenuOpen || root.episodeListOpen)
            return
        root.cursorHidden = true
    }

    function scheduleAutoHide() {
        if (root.uiPinned || root.stickyEpisodeList || root.audioMenuOpen || root.subtitleMenuOpen || root.delayPanelOpen)
            return
        hideTimer.restart()
    }

    onActiveChanged: {
        if (!root.active)
            root.showCursor()
    }
    onUiPinnedChanged: {
        if (root.uiPinned) {
            root.showCursor()
            hideTimer.stop()
        } else {
            root.scheduleAutoHide()
        }
    }
    onCinemaModeChanged: {
        if (root.cinemaMode && root.active)
            root.scheduleAutoHide()
    }
    onEpisodeListOpenChanged: {
        if (root.episodeListOpen)
            root.showCursor()
    }

    function closeTrackMenus() {
        root.audioMenuOpen = false
        root.subtitleMenuOpen = false
    }

    function toggleAudioMenu() {
        if (root.audioMenuOpen) {
            root.audioMenuOpen = false
            if (!root.episodeListOpen)
                hideTimer.restart()
            return
        }
        root.closeTrackMenus()
        root.stickBottomChrome()
        root.audioMenuOpen = true
    }

    function toggleSubtitleMenu() {
        if (root.subtitleMenuOpen) {
            root.subtitleMenuOpen = false
            if (!root.episodeListOpen)
                hideTimer.restart()
            return
        }
        root.closeTrackMenus()
        root.stickBottomChrome()
        root.subtitleMenuOpen = true
    }

    function pickAudioTrack(id, title) {
        player.setAudioTrack(id)
        root.audioMenuOpen = false
        trackFlash.flashLabel = "🔊 " + title
        trackFlashAnim.restart()
        if (!root.episodeListOpen)
            hideTimer.restart()
    }

    function pickSubtitleTrack(id, title) {
        player.setSubtitleTrack(id)
        root.subtitleMenuOpen = false
        trackFlash.flashLabel = id < 0 ? "CC Выкл" : ("CC " + title)
        trackFlashAnim.restart()
        if (!root.episodeListOpen)
            hideTimer.restart()
    }

    function scrollTrackList(listView, wheel) {
        wheel.accepted = true
        hideTimer.stop()
        root.stickBottomChrome()
        var maxY = Math.max(0, listView.contentHeight - listView.height)
        var step = (wheel.angleDelta.y > 0 ? -1 : 1) * 34
        listView.contentY = Math.max(0, Math.min(maxY, listView.contentY + step))
    }

    function stickBottomChrome() {
        hideTimer.stop()
        root.stickyBottom = true
        root.bottomOpacity = 1
    }

    function scrollEpisodeList(wheel) {
        wheel.accepted = true
        hideTimer.stop()
        root.stickyEpisodeList = true
        root.stickBottomChrome()
        var maxY = Math.max(0, episodeListView.contentHeight - episodeListView.height)
        var step = (wheel.angleDelta.y > 0 ? -1 : 1) * 34
        episodeListView.contentY = Math.max(0, Math.min(maxY, episodeListView.contentY + step))
    }

    function formatTime(seconds, remaining) {
        if (!seconds || seconds < 0 || isNaN(seconds))
            return "0:00"
        var s = Math.floor(seconds)
        if (remaining)
            s = Math.max(0, s)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        var mm = String(m).padStart(2, "0")
        var ss = String(sec).padStart(2, "0")
        return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
    }

    function smoothstep(t) {
        t = Math.max(0, Math.min(1, t))
        return t * t * (3 - 2 * t)
    }

    function edgeOpacity(distFromEdge, inDist, outDist) {
        if (distFromEdge <= inDist)
            return 1
        if (distFromEdge >= outDist)
            return 0
        var t = (distFromEdge - inDist) / (outDist - inDist)
        return 1 - smoothstep(t)
    }

    function updateStickyZones(distTop, distBottom, distRight) {
        if (distTop <= root.topOut)
            root.stickyTop = true
        else if (root.stickyTop && distTop > root.topBarH + root.zoneStickPx)
            root.stickyTop = false

        if (distBottom <= root.bottomOut)
            root.stickyBottom = true
        else if (root.stickyBottom && distBottom > root.bottomZoneH + root.zoneStickPx)
            root.stickyBottom = false

        if (distRight <= root.volumeOut)
            root.stickyVolume = true
        else if (root.stickyVolume && distRight > root.volumeW + root.zoneStickPx)
            root.stickyVolume = false
    }

    function setTargets(mx, my) {
        if (!root.active || root.uiPinned)
            return

        var distTop = my
        var distBottom = root.height - my
        var distRight = root.width - mx

        root.updateStickyZones(distTop, distBottom, distRight)

        var topT = root.stickyTop ? 1 : edgeOpacity(distTop, topIn, topOut)
        var bottomT = root.stickyBottom ? 1 : edgeOpacity(distBottom, bottomIn, bottomOut)
        var volumeT = root.stickyVolume ? 1 : edgeOpacity(distRight, volumeIn, volumeOut)

        if ((root.stickyBottom || bottomT > 0.7) && distRight < volumeIn + 24)
            volumeT = Math.max(volumeT, root.stickyBottom ? 1 : Math.min(1, bottomT * 0.85))

        root.topBarOpacity = topT
        root.bottomOpacity = bottomT
        root.volumeOpacity = volumeT
    }

    function clearStickyZones() {
        root.stickyTop = false
        root.stickyBottom = false
        root.stickyVolume = false
        if (!root.episodeListOpen)
            root.stickyEpisodeList = false
    }

    function hideAllTargets() {
        if (root.uiPinned)
            return
        if (!root.stickyEpisodeList)
            root.episodeListOpen = false
        root.closeTrackMenus()
        root.topBarOpacity = 0
        root.bottomOpacity = 0
        root.volumeOpacity = 0
        root.clearStickyZones()
        root.hideCursorNow()
    }

    function toggleCinema() {
        root.requestCinemaToggle()
    }

    function handleWheel(wheel) {
        if (!root.active)
            return
        // Ретро: плеер встроен в прокручиваемую страницу — вне cinema колесо
        // уходит странице (скролл), а не крутит громкость.
        if (root.wheelPassThroughOutsideCinema && !root.cinemaMode) {
            wheel.accepted = false
            return
        }
        if (root.episodeListOpen && wheel.y >= episodeListPanel.y)
            return root.scrollEpisodeList(wheel)
        if (root.audioMenuOpen)
            return root.scrollTrackList(audioTrackList, wheel)
        if (root.subtitleMenuOpen)
            return root.scrollTrackList(subtitleList, wheel)
        hideTimer.stop()
        var mx = (wheel && wheel.x !== undefined) ? wheel.x : root.mouseX
        var my = (wheel && wheel.y !== undefined) ? wheel.y : root.mouseY
        if (mx >= 0 && my >= 0)
            root.setTargets(mx, my)
        root.scheduleAutoHide()
        if (scrubFlash.active)
            return
        if (wheel.modifiers & Qt.ShiftModifier) {
            var step = wheel.angleDelta.y > 0 ? 10 : -10
            player.seekRelative(step)
            seekFlash.flashLabel = (step > 0 ? "+" : "") + step + "s"
            seekFlashAnim.restart()
        } else {
            var vstep = wheel.angleDelta.y > 0 ? 5 : -5
            root.adjustVolume(vstep)
        }
    }

    Timer {
        id: hideTimer
        interval: root.hideDelayMs
        onTriggered: {
            if (root.uiPinned || root.stickyEpisodeList || root.audioMenuOpen || root.subtitleMenuOpen || root.delayPanelOpen)
                return
            root.hideAllTargets()
        }
    }

    Timer {
        id: leftClickTimer
        // ~250 мс как в mpv/uosc: одиночный клик подтверждается только если за
        // это время не пришёл второй клик (двойной клик ловится в onDoubleClicked).
        interval: 250
        onTriggered: {
            if (!root.active)
                return
            player.paused = !player.paused
            pauseFlashAnim.restart()
        }
    }

    function togglePause() {
        if (!root.active)
            return
        player.paused = !player.paused
        pauseFlashAnim.restart()
    }

    function seekSeconds(step) {
        if (!root.active)
            return
        player.seekRelative(step)
        seekFlash.flashLabel = (step > 0 ? "+" : "−") + Math.abs(step) + "s"
        seekFlashAnim.restart()
        root.showCursor()
        root.scheduleAutoHide()
    }

    function adjustVolume(step) {
        if (!root.active)
            return
        player.volume = Math.max(0, Math.min(100, player.volume + step))
        root.stickyVolume = true
        root.volumeOpacity = 1
        volumeFlash.flashLabel = "🔊 " + player.volume + "%"
        volumeFlashAnim.restart()
        root.showCursor()
    }

    Shortcut {
        sequence: "Space"
        enabled: root.active
        onActivated: root.togglePause()
    }

    Shortcut {
        sequence: "F"
        enabled: root.active
        onActivated: {
            root.showCursor()
            root.toggleCinema()
            root.scheduleAutoHide()
        }
    }

    Shortcut {
        sequence: "Left"
        enabled: root.active
        onActivated: root.seekSeconds(-10)
    }

    Shortcut {
        sequence: "Right"
        enabled: root.active
        onActivated: root.seekSeconds(10)
    }

    Shortcut {
        sequence: "M"
        enabled: root.active
        onActivated: {
            player.muted = !player.muted
            volumeFlash.flashLabel = player.muted ? "🔇 muted" : "🔊 " + player.volume + "%"
            volumeFlashAnim.restart()
            root.showCursor()
            root.scheduleAutoHide()
        }
    }

    Shortcut {
        sequence: "Up"
        enabled: root.active
        onActivated: root.adjustVolume(5)
    }

    Shortcut {
        sequence: "Down"
        enabled: root.active
        onActivated: root.adjustVolume(-5)
    }

    // ESC: закрыть меню/список, затем — выйти из полноэкранного режима.
    Shortcut {
        sequence: "Escape"
        enabled: root.active
        onActivated: {
            if (root.audioMenuOpen || root.subtitleMenuOpen || root.episodeListOpen) {
                root.closeTrackMenus()
                root.episodeListOpen = false
                root.stickyEpisodeList = false
            } else if (root.cinemaMode) {
                root.requestCinemaToggle()
            }
        }
    }

    // Постер тайтла на время loading/смены серии — вместо чёрного экрана.
    // Статичный (не слайдшоу), скрывается как только пошли кадры. Под всем
    // хромом (z=0), поверх видео.
    Image {
        id: posterLoading
        anchors.fill: parent
        visible: root.posterSource !== "" && (!root.player.hasMedia || root.player.loading)
        source: root.posterSource
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        clip: true
        Rectangle {
            anchors.fill: parent
            color: "#55000000"
        }
    }

    // --- Слой ввода (поверх видео, под UI)
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: root.cursorHidden ? Qt.BlankCursor : Qt.ArrowCursor
        z: 1

        onPositionChanged: function(mouse) {
            root.mouseInside = true
            root.showCursor()
            root.mouseX = mouse.x
            root.mouseY = mouse.y
            root.setTargets(mouse.x, mouse.y)
            root.scheduleAutoHide()
            // Продолжаем скраб, если кнопка была зажата на таймлайне, а курсор
            // ушёл вверх/вниз с 40px-полоски — полоска сама события уже не
            // получает.
            if (uoscTimeline.scrubbing)
                uoscTimeline.seekFromGlobalX(mouse.x)
        }
        onEntered: {
            root.mouseInside = true
            root.showCursor()
            if (root.mouseX >= 0 && root.mouseY >= 0)
                root.setTargets(root.mouseX, root.mouseY)
            root.scheduleAutoHide()
        }
        onExited: {
            root.mouseInside = false
            root.scheduleAutoHide()
        }

        onClicked: function(mouse) {
            if (!root.active) return
            if (mouse.button === Qt.LeftButton) {
                root.lastClickPos = Qt.point(mouse.x, mouse.y)
                root.lastClickTime = Date.now()
                leftClickTimer.restart()
            } else if (mouse.button === Qt.RightButton) {
                root.uiPinned = !root.uiPinned
                hideTimer.stop()
                if (root.uiPinned) {
                    root.showCursor()
                    root.topBarOpacity = 1
                    root.bottomOpacity = 1
                    root.volumeOpacity = 1
                } else if (root.mouseInside) {
                    root.setTargets(root.mouseX, root.mouseY)
                } else {
                    root.hideAllTargets()
                }
            }
        }

        onDoubleClicked: function(mouse) {
            if (!root.active || mouse.button !== Qt.LeftButton)
                return
            leftClickTimer.stop()
            // Qt считает двойным кликом клики в пределах ~400 мс — из-за этого
            // быстрые pause/play уходили в fullscreen. Ужесточаем как в mpv/uosc:
            // двойной клик засчитывается только если клики быстрые (<=250 мс)
            // и курсор почти не сдвинулся. Иначе — это два одиночных клика.
            var elapsed = Date.now() - root.lastClickTime
            var dist = Math.hypot(mouse.x - root.lastClickPos.x, mouse.y - root.lastClickPos.y)
            if (elapsed > 250 || dist > 24) {
                togglePause()
                return
            }
            root.toggleCinema()
        }

        onWheel: function(wheel) { root.handleWheel(wheel) }
    }

    // --- Верх: мягкий градиент + заголовок
    Item {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.topBarH
        opacity: root.uiPinned ? 1 : root.topBarOpacity
        visible: root.active && opacity > 0.01
        z: 3
        Behavior on opacity { SmoothedAnimation { velocity: root.smoothVelocity; maximumEasingTime: root.smoothMaxTime } }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.shadeTop }
                GradientStop { position: 0.55; color: "#66000000" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 100
            elide: Text.ElideRight
            color: root.fg
            font.pixelSize: 13
            text: root.headerTitle
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onEntered: {
                hideTimer.stop()
                root.stickyTop = true
                root.topBarOpacity = 1
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            UoscButton {
                iconSource: root.cinemaMode ? root.playerIcon("fullscreen-exit.svg") : root.playerIcon("fullscreen.svg")
                size: 28
                onClicked: root.toggleCinema()
            }
        }
    }

    // --- Громкость справа (градиент только при показе)
    Item {
        id: volumeBar
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: root.volumeW
        opacity: root.uiPinned ? 1 : root.volumeOpacity
        visible: root.active && opacity > 0.01
        z: 3
        Behavior on opacity { SmoothedAnimation { velocity: root.smoothVelocity; maximumEasingTime: root.smoothMaxTime } }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: "#99000000" }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 6

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: root.playerIcon("volume.svg")
                width: 18
                height: 18
                sourceSize: Qt.size(36, 36)
                fillMode: Image.PreserveAspectFit
                opacity: 0.92
            }

            Item {
                width: 28
                height: 120
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 3
                    height: parent.height
                    radius: 2
                    color: root.trackBg
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * (player.volume / 100)
                        radius: 2
                        color: root.trackFill
                        Behavior on height { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                    }
                }

                Slider {
                    anchors.fill: parent
                    orientation: Qt.Vertical
                    from: 0
                    to: 100
                    value: player.volume
                    opacity: 0.001
                    background: Item {}
                    onValueChanged: if (pressed) player.volume = Math.round(value)
                    onPressedChanged: if (!pressed) {
                        volumeFlash.flashLabel = "🔊 " + player.volume + "%"
                        volumeFlashAnim.restart()
                    }
                }
            }

            Text {
                text: player.volume + "%"
                color: root.fgMuted
                font.pixelSize: 10
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onEntered: {
                hideTimer.stop()
                root.stickyVolume = true
                root.volumeOpacity = 1
            }
        }
    }

    // --- Низ: градиент + контролы (таймлайн отдельно у самого края)
    Item {
        id: bottomChrome
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.bottomChromeH
        z: 4
        visible: root.active

        // Градиент на всю нижнюю зону (контролы + поле перемотки), как в uosc
        readonly property real chromeOpacity: root.uiPinned ? 1 : root.bottomOpacity

        // Плавный переход к видео — только верхняя полоса, не накладывается на таймлайн
        Rectangle {
            id: bottomShade
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: bottomBlock.top
            opacity: parent.chromeOpacity
            visible: opacity > 0.01
            z: 0
            Behavior on opacity { SmoothedAnimation { velocity: root.smoothVelocity; maximumEasingTime: root.smoothMaxTime } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.45; color: "#55000000" }
                GradientStop { position: 1.0; color: root.shadeBottom }
            }
        }

        // Единый тёмный низ: контролы + зона таймлайна без второго полупрозрачного слоя
        Rectangle {
            id: bottomSolid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.top: bottomBlock.top
            color: root.shadeBottom
            opacity: parent.chromeOpacity
            visible: opacity > 0.01
            z: 1
            Behavior on opacity { SmoothedAnimation { velocity: root.smoothVelocity; maximumEasingTime: root.smoothMaxTime } }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            z: 0
            onPositionChanged: function(mouse) {
                hideTimer.stop()
                root.mouseInside = true
                root.mouseX = mouse.x
                root.mouseY = root.height - (parent.height - mouse.y)
                root.stickBottomChrome()
            }
            onEntered: root.stickBottomChrome()
        }

        Item {
            id: episodeListZone
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.bottom: bottomBlock.top
            anchors.bottomMargin: 2
            width: episodeListPanel.width + 8
            height: episodeListPanel.height + bottomBlock.height + 10
            visible: root.episodeListOpen && root.effectiveMaxEpisode > 0
            z: 9

            function stickOpen() {
                root.episodeListOpen = true
                root.stickyEpisodeList = true
                root.stickBottomChrome()
                Qt.callLater(function() {
                    if (episodeListView.count > 0) {
                        var idx = root.playback ? Math.max(0, root.playback.currentEpisode - 1) : 0
                        episodeListView.positionViewAtIndex(idx, ListView.Center)
                    }
                })
            }

            Timer {
                id: episodeListUnstick
                interval: 120
                onTriggered: {
                    if (!episodeListZoneHover.containsMouse)
                        root.stickyEpisodeList = false
                }
            }

            Rectangle {
                id: episodeListPanel
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                width: 228
                height: Math.min(root.effectiveMaxEpisode * 34 + 10, 260)
                radius: 6
                color: "#d91a1a1a"
                border.color: "#30ffffff"
                border.width: 1
                opacity: bottomChrome.chromeOpacity
                visible: opacity > 0.01
                clip: true

                ListView {
                    id: episodeListView
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: root.effectiveMaxEpisode
                    currentIndex: root.playback ? Math.max(0, root.playback.currentEpisode - 1) : 0
                    delegate: Rectangle {
                        width: episodeListView.width
                        height: 30
                        radius: 3
                        color: (index + 1) === (root.playback ? root.playback.currentEpisode : 0)
                            ? "#33ffffff"
                            : (epMouse.containsMouse ? "#22ffffff" : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.fg
                            font.pixelSize: 12
                            text: "Серия " + (index + 1)
                        }

                        MouseArea {
                            id: epMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.requestEpisode(index + 1)
                                root.episodeListOpen = false
                                root.stickyEpisodeList = false
                            }
                        }
                    }
                }
            }

            MouseArea {
                id: episodeListZoneHover
                anchors.fill: parent
                z: 2
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                onEntered: episodeListZone.stickOpen()
                onExited: episodeListUnstick.restart()
                onWheel: function(wheel) {
                    if (wheel.y >= episodeListPanel.y)
                        root.scrollEpisodeList(wheel)
                    else
                        wheel.accepted = true
                }
            }
        }

        MouseArea {
            visible: root.episodeListOpen
            anchors.fill: parent
            z: 8
            onClicked: {
                root.episodeListOpen = false
                root.stickyEpisodeList = false
            }
        }

        // Контролы внизу; таймлайн — НАД ними (полоска вверху, область
        // скраба под ней).
        Item {
            id: bottomBlock
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            height: root.controlsH + 8
            opacity: parent.chromeOpacity
            visible: opacity > 0.01
            z: 2
            Behavior on opacity { SmoothedAnimation { velocity: root.smoothVelocity; maximumEasingTime: root.smoothMaxTime } }

            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                height: root.controlsH

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    UoscButton {
                        iconSource: root.playerIcon("menu.svg")
                        size: 32
                        visible: root.showEpisodeList
                        active: root.episodeListOpen
                        buttonEnabled: root.effectiveMaxEpisode > 0
                        onClicked: {
                            root.closeTrackMenus()
                            root.episodeListOpen = !root.episodeListOpen
                            if (root.episodeListOpen)
                                episodeListZone.stickOpen()
                            else
                                root.stickyEpisodeList = false
                        }
                    }

                    // Название тайтла; время — у конца таймлайна (см. uoscTimeline).
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(280, bottomBlock.width * 0.34)
                        elide: Text.ElideRight
                        color: root.fg
                        font.family: root.fontFamily
                        font.pixelSize: 12
                        font.bold: true
                        text: root.headerTitle
                    }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    UoscButton {
                        iconSource: Qt.resolvedUrl("../assets/chevron-left.svg")
                        size: 36
                        buttonEnabled: root.playback && root.playback.currentEpisode > 1
                        onClicked: root.requestEpisode(root.playback.currentEpisode - 1)
                    }
                    UoscButton {
                        iconSource: player.paused ? root.playerIcon("play.svg") : root.playerIcon("pause.svg")
                        size: 40
                        onClicked: { player.paused = !player.paused; pauseFlashAnim.restart() }
                    }
                    UoscButton {
                        iconSource: Qt.resolvedUrl("../assets/chevron-right.svg")
                        size: 36
                        buttonEnabled: root.playback && root.playback.currentEpisode < root.effectiveMaxEpisode
                        onClicked: root.requestEpisode(root.playback.currentEpisode + 1)
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    // Пропуск опенинга (+1:27) — на месте бывших −10/+10.
                    UoscButton {
                        label: "OP"
                        size: 32
                        visible: root.showSkipOpening
                        onClicked: player.seekRelative(87)
                    }

                    UoscButton {
                        label: "DLY"
                        size: 32
                        active: delayPanelOpen
                        onClicked: root.delayPanelOpen = !root.delayPanelOpen
                    }

                    UoscButton {
                        iconSource: root.playerIcon("audio.svg")
                        size: 32
                        active: root.audioMenuOpen
                        buttonEnabled: player.audioTracks.length > 1
                        onClicked: {
                            if (player.audioTracks.length > 1)
                                root.toggleAudioMenu()
                        }
                    }
                    UoscButton {
                        iconSource: root.playerIcon("subtitles.svg")
                        size: 32
                        active: root.subtitleMenuOpen
                        buttonEnabled: player.subtitleTracks.length > 0
                        onClicked: {
                            if (player.subtitleTracks.length > 0)
                                root.toggleSubtitleMenu()
                        }
                    }

                    Item { width: 4; height: 1 }

                    UoscButton {
                        label: player.speed === 1 ? "1×" : player.speed.toFixed(2) + "×"
                        size: 38
                        onClicked: {
                            var speeds = [0.75, 1, 1.25, 1.5, 1.75, 2]
                            var idx = speeds.indexOf(player.speed)
                            if (idx < 0) idx = 1
                            player.speed = speeds[(idx + 1) % speeds.length]
                            speedFlash.flashLabel = player.speed.toFixed(2) + "×"
                            speedFlashAnim.restart()
                        }
                    }

                    UoscButton {
                        iconSource: root.cinemaMode ? root.playerIcon("fullscreen-exit.svg") : root.playerIcon("fullscreen.svg")
                        size: 32
                        onClicked: root.toggleCinema()
                    }
                }
            }
        }

        // === Попап задержки звука (audioDelay) ===
        Rectangle {
            id: delayPanel
            visible: root.delayPanelOpen
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: bottomBlock.top
            anchors.bottomMargin: 8
            width: 230
            height: delayCol.implicitHeight + 16
            radius: 8
            color: "#e0202020"
            border.width: 1
            border.color: "#4a4a4a"
            z: 12
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton }

            Column {
                id: delayCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Text {
                    text: "Задержка звука: " + (player.audioDelay >= 0 ? "+" : "") + player.audioDelay.toFixed(1) + "s"
                    color: root.fg
                    font.family: root.fontFamily
                    font.pixelSize: 12
                }
                Row {
                    width: parent.width
                    spacing: 6
                    UoscButton { label: "−0.1"; size: 34; onClicked: player.audioDelay = Math.round((player.audioDelay - 0.1) * 10) / 10 }
                    UoscButton { label: "+0.1"; size: 34; onClicked: player.audioDelay = Math.round((player.audioDelay + 0.1) * 10) / 10 }
                    UoscButton { label: "0"; size: 34; onClicked: player.audioDelay = 0 }
                }
                UoscButton {
                    label: "SAVE PRESET"
                    size: 34
                    visible: root.audioSyncTitleId !== ""
                    onClicked: appConfig.setAudioSyncOffset(root.audioSyncTitleId, player.audioDelay)
                }
            }
        }

        // Таймлайн НАД контролами: полоска вверху, область скраба под ней.
        UoscTimeline {
            id: uoscTimeline
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: bottomBlock.top
            height: root.timelineHitH
            player: root.player
            z: 3
            chromeActive: parent.chromeOpacity
            trackColor: root.trackBg
            progressColor: root.trackFill
            fontFamily: root.fontFamily
            onHoverChanged: function(hovered) {
                if (!hovered)
                    return
                hideTimer.stop()
                root.stickyBottom = true
                root.bottomOpacity = 1
            }
            onInteractionStarted: hideTimer.stop()
            onInteractionEnded: root.scheduleAutoHide()
            onSeekPreviewChanged: function(targetTime, delta, active) {
                scrubFlash.targetTime = targetTime
                scrubFlash.deltaText = delta
                scrubFlash.active = active
                if (active) {
                    seekFlashAnim.stop()
                    seekFlash.opacity = 0
                    volumeFlashAnim.stop()
                    volumeFlash.opacity = 0
                    speedFlashAnim.stop()
                    speedFlash.opacity = 0
                }
            }
            onWheel: function(wheel) { root.handleWheel(wheel) }
        }

        // === Превью на таймлайне (#20): кадр снимается на тихом mpv_handle
        // (ThumbnailProbe), основной плеер не seek-ается. Debounce 100 мс.
        // Нет кадра (ещё грузится/источник не отдал) — только время, без
        // чёрной плашки.
        Item {
            id: thumbPopup
            // pointerX >= 0: после отпускания таймлайна pointerX = -1, иначе
            // попап (x = pointerX - width/2 -> clamp 0) прыгал бы над начало
            // полоски на мгновение, пока курсор ещё над ней.
            visible: uoscTimeline.hovered && uoscTimeline.pointerX >= 0 && uoscTimeline.duration > 0
            readonly property bool hasFrame: thumbProbe.frameVersion > 0
            width: thumbPopup.hasFrame ? 176 : (thumbTimePill.width + 12)
            height: thumbPopup.hasFrame ? 99 : (thumbTimePill.height + 4)
            x: Math.max(0, Math.min(uoscTimeline.width - width,
                                    uoscTimeline.pointerX - width / 2))
            y: uoscTimeline.y - height - 6
            z: 20
            Rectangle {
                anchors.fill: parent
                radius: 8
                color: "#d8000000"
                border.color: "#4a4a4a"
                visible: thumbPopup.hasFrame
            }
            Image {
                id: thumbImage
                anchors.fill: parent
                anchors.margins: 3
                visible: thumbPopup.hasFrame
                source: "image://thumbs/frame?v=" + thumbProbe.frameVersion
                cache: false
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                id: thumbTimePill
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: thumbTimeLabel.width + 12
                height: thumbTimeLabel.height + 4
                radius: 4
                color: "#cc000000"
                Text {
                    id: thumbTimeLabel
                    anchors.centerIn: parent
                    text: uoscTimeline.previewTimeText
                    color: "white"
                    font.pixelSize: 11
                }
            }
            Timer {
                id: thumbDebounce
                interval: 100
                onTriggered: {
                    if (root.playback && uoscTimeline.hovered && uoscTimeline.duration > 0)
                        root.playback.requestThumbnail(uoscTimeline.previewSeconds)
                }
            }
            Connections {
                target: uoscTimeline
                function onPointerXChanged() { thumbDebounce.restart() }
            }
        }
    }

    // --- Выбор озвучки/субтитров по центру (как громкость / перемотка)
    Item {
        anchors.fill: parent
        visible: root.audioMenuOpen || root.subtitleMenuOpen
        z: 11

        MouseArea {
            anchors.fill: parent
            onClicked: root.closeTrackMenus()
            onWheel: function(wheel) { root.handleWheel(wheel) }
        }

        Rectangle {
            id: audioPicker
            anchors.centerIn: parent
            visible: root.audioMenuOpen
            width: Math.min(340, root.width - 48)
            height: Math.min(44 + audioTrackList.contentHeight, root.height * 0.55)
            radius: root.borderRadius
            color: "#90000000"
            border.color: "#28ffffff"
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: root.fg
                    font.pixelSize: 14
                    font.bold: true
                    text: "Озвучка"
                }

                ListView {
                    id: audioTrackList
                    width: parent.width
                    height: parent.height - 28
                    clip: true
                    model: player.audioTracks
                    spacing: 2
                    delegate: Rectangle {
                        width: audioTrackList.width
                        height: 34
                        radius: 3
                        color: modelData.selected ? "#33ffffff" : (trackMouse.containsMouse ? "#22ffffff" : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 20
                            elide: Text.ElideRight
                            color: modelData.selected ? root.fg : root.fgMuted
                            font.pixelSize: 13
                            text: modelData.title
                        }

                        MouseArea {
                            id: trackMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.pickAudioTrack(modelData.id, modelData.title)
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: function(wheel) { root.scrollTrackList(audioTrackList, wheel) }
            }
        }

        Rectangle {
            id: subtitlePicker
            anchors.centerIn: parent
            visible: root.subtitleMenuOpen
            width: Math.min(340, root.width - 48)
            height: Math.min(44 + subtitleOffRow.height + subtitleList.contentHeight, root.height * 0.55)
            radius: root.borderRadius
            color: "#90000000"
            border.color: "#28ffffff"
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: root.fg
                    font.pixelSize: 14
                    font.bold: true
                    text: "Субтитры"
                }

                Rectangle {
                    id: subtitleOffRow
                    width: parent.width
                    height: 34
                    radius: 3
                    color: offMouse.containsMouse ? "#22ffffff" : "transparent"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.fgMuted
                        font.pixelSize: 13
                        text: "Выкл"
                    }

                    MouseArea {
                        id: offMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.pickSubtitleTrack(-1, "")
                    }
                }

                ListView {
                    id: subtitleList
                    width: parent.width
                    height: parent.height - subtitleOffRow.height - 34
                    clip: true
                    model: player.subtitleTracks
                    spacing: 2
                    delegate: Rectangle {
                        width: subtitleList.width
                        height: 34
                        radius: 3
                        color: modelData.selected ? "#33ffffff" : (subMouse.containsMouse ? "#22ffffff" : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 20
                            elide: Text.ElideRight
                            color: modelData.selected ? root.fg : root.fgMuted
                            font.pixelSize: 13
                            text: modelData.title
                        }

                        MouseArea {
                            id: subMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.pickSubtitleTrack(modelData.id, modelData.title)
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: function(wheel) { root.scrollTrackList(subtitleList, wheel) }
            }
        }
    }

    // --- Flash indicators (минимальные)
    Rectangle {
        id: pauseFlash
        anchors.centerIn: parent
        width: 68; height: 68
        radius: 34
        color: "#66000000"
        opacity: 0
        visible: opacity > 0.001
        z: 10
        Image {
            anchors.centerIn: parent
            source: player.paused ? root.playerIcon("pause.svg") : root.playerIcon("play.svg")
            width: 28; height: 28
            sourceSize: Qt.size(56, 56)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        SequentialAnimation {
            id: pauseFlashAnim
            NumberAnimation { target: pauseFlash; property: "opacity"; to: 1; duration: 130; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 420 }
            NumberAnimation { target: pauseFlash; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InOutCubic }
        }
    }

    Rectangle {
        id: volumeFlash
        property string flashLabel: ""
        anchors.centerIn: parent
        width: 116; height: 40
        radius: root.borderRadius
        color: "#77000000"
        opacity: 0
        visible: opacity > 0.001
        z: 10
        Text { anchors.centerIn: parent; text: volumeFlash.flashLabel; color: root.fg; font.pixelSize: 15 }
        SequentialAnimation {
            id: volumeFlashAnim
            NumberAnimation { target: volumeFlash; property: "opacity"; to: 1; duration: 130; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 480 }
            NumberAnimation { target: volumeFlash; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InOutCubic }
        }
    }

    Rectangle {
        id: speedFlash
        property string flashLabel: ""
        anchors.centerIn: parent
        width: 84; height: 40
        radius: root.borderRadius
        color: "#77000000"
        opacity: 0
        visible: opacity > 0.001
        z: 10
        Text { anchors.centerIn: parent; text: speedFlash.flashLabel; color: root.fg; font.pixelSize: 17 }
        SequentialAnimation {
            id: speedFlashAnim
            NumberAnimation { target: speedFlash; property: "opacity"; to: 1; duration: 130; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 480 }
            NumberAnimation { target: speedFlash; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InOutCubic }
        }
    }

    Rectangle {
        id: scrubFlash
        property string targetTime: ""
        property string deltaText: ""
        property bool active: false
        anchors.centerIn: parent
        width: Math.max(120, Math.max(scrubTimeText.implicitWidth, scrubDeltaText.implicitWidth) + 32)
        height: 58
        radius: root.borderRadius
        color: "#90000000"
        border.color: "#28ffffff"
        border.width: 1
        opacity: scrubFlash.active ? 1 : 0
        visible: scrubFlash.active || opacity > 0.001
        z: 12
        Behavior on opacity { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

        Column {
            anchors.centerIn: parent
            spacing: 4

            Text {
                id: scrubTimeText
                anchors.horizontalCenter: parent.horizontalCenter
                text: scrubFlash.targetTime
                color: root.fg
                font.pixelSize: 18
                font.bold: true
            }
            Text {
                id: scrubDeltaText
                anchors.horizontalCenter: parent.horizontalCenter
                text: scrubFlash.deltaText
                color: root.fgMuted
                font.pixelSize: 13
            }
        }
    }

    Rectangle {
        id: seekFlash
        property string flashLabel: ""
        anchors.centerIn: parent
        width: 76; height: 40
        radius: root.borderRadius
        color: "#77000000"
        opacity: 0
        visible: opacity > 0.001
        z: 10
        Text { anchors.centerIn: parent; text: seekFlash.flashLabel; color: root.fg; font.pixelSize: 15 }
        SequentialAnimation {
            id: seekFlashAnim
            NumberAnimation { target: seekFlash; property: "opacity"; to: 1; duration: 130; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 380 }
            NumberAnimation { target: seekFlash; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InOutCubic }
        }
    }

    Rectangle {
        id: trackFlash
        property string flashLabel: ""
        anchors.centerIn: parent
        width: Math.max(120, trackFlashText.implicitWidth + 32)
        height: 40
        radius: root.borderRadius
        color: "#77000000"
        opacity: 0
        visible: opacity > 0.001
        z: 10
        Text {
            id: trackFlashText
            anchors.centerIn: parent
            text: trackFlash.flashLabel
            color: root.fg
            font.pixelSize: 15
            elide: Text.ElideRight
            width: Math.min(implicitWidth, root.width - 64)
        }
        SequentialAnimation {
            id: trackFlashAnim
            NumberAnimation { target: trackFlash; property: "opacity"; to: 1; duration: 130; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 480 }
            NumberAnimation { target: trackFlash; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InOutCubic }
        }
    }
}