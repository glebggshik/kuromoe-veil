import QtQuick
import QtQuick.Controls

// Таймлайн uosc: тонкая линия у края + превью времени при перемотке.
Item {
    id: root

    required property var player

    property int collapsedHeight: 3
    property int expandedHeight: 12
    property int hitHeight: 40
    property color trackColor: "#80000000"
    property color progressColor: "#ffffff"
    property real chromeActive: 0
    property string fontFamily: ""

    property real pointerX: -1
    property real dragStartSeconds: 0

    readonly property bool hovered: mouseArea.containsMouse || mouseArea.pressed
    readonly property bool scrubbing: mouseArea.pressed
    readonly property bool chromeExpanded: root.chromeActive > 0.01
    readonly property int progressLineH: mouseArea.pressed ? root.expandedHeight : root.collapsedHeight

    readonly property real duration: (player && player.duration > 0) ? player.duration : 0
    readonly property real progress: duration > 0
        ? Math.max(0, Math.min(1, player.position / duration))
        : 0
    readonly property real previewRatio: {
        if (root.pointerX < 0 || root.duration <= 0 || mouseArea.width <= 0)
            return root.progress
        return Math.max(0, Math.min(1, root.pointerX / mouseArea.width))
    }
    readonly property real previewSeconds: root.previewRatio * root.duration
    readonly property real previewDelta: root.previewSeconds - (root.scrubbing ? root.dragStartSeconds : player.position)
    readonly property string previewTimeText: root.formatTime(root.previewSeconds)
    readonly property string previewDeltaText: root.formatDelta(root.previewDelta)

    implicitHeight: hitHeight

    signal hoverChanged(bool hovered)
    signal interactionStarted()
    signal interactionEnded()
    signal seekPreviewChanged(string targetTime, string delta, bool active)
    signal wheel(var wheelEvent)

    function pushPreview(active) {
        root.seekPreviewChanged(
            active ? root.previewTimeText : "",
            active ? root.previewDeltaText : "",
            active)
    }

    // Скраб не должен срываться, когда курсор уходит с полоски вверх/вниз:
    // вызывается из слоя ввода PlayerOverlay (покрывает весь плеер) и
    // продолжает вести seek по горизонтали, пока кнопка зажата.
    function seekFromGlobalX(globalX) {
        if (!root.scrubbing || !root.player || root.duration <= 0)
            return
        var localX = globalX - root.x // общая система координат с PlayerOverlay
        root.pointerX = localX
        var ratio = Math.max(0, Math.min(1, localX / root.width))
        root.player.seek(ratio * root.duration)
    }

    onPreviewSecondsChanged: if (root.scrubbing) root.pushPreview(true)

    function formatTime(seconds) {
        if (!seconds || seconds < 0 || isNaN(seconds))
            return "0:00"
        var s = Math.floor(Math.abs(seconds))
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        var mm = String(m).padStart(2, "0")
        var ss = String(sec).padStart(2, "0")
        return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
    }

    function formatDelta(seconds) {
        if (Math.abs(seconds) < 1)
            return "0s"
        var sign = seconds >= 0 ? "+" : "−"
        var s = Math.floor(Math.abs(seconds))
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60

        if (s < 60)
            return sign + sec + "s"
        if (h === 0) {
            if (sec === 0)
                return sign + m + "min"
            return sign + m + "min " + sec + "s"
        }

        var label = sign + h + "h"
        if (m > 0)
            label += " " + m + "min"
        if (sec > 0)
            label += " " + sec + "s"
        return label
    }

    // 1. Трек — тонкая полоса у ВЕРХНЕГО края зоны; ниже — область скраба
    Rectangle {
        id: backgroundTrack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.progressLineH
        radius: 2
        color: root.chromeExpanded ? "transparent" : root.trackColor
        clip: true

        // Текущая позиция (тусклее, чтобы не сливалась с превью)
        Rectangle {
            id: progressBar
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: backgroundTrack.width * root.progress
            color: root.progressColor
            opacity: root.scrubbing ? 0.35 : 1

            Behavior on width {
                enabled: !mouseArea.pressed
                NumberAnimation { duration: 55; easing.type: Easing.OutCubic }
            }
        }

        // Текущее время — у правого конца таймлайна (вне clip-контейнера).
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.top: backgroundTrack.top
            text: root.formatTime(root.player ? root.player.position : 0)
            color: "#ffffff"
            font.family: root.fontFamily
            font.pixelSize: 11
        }

        // Превью перемотки — отдельная полоса
        Rectangle {
            visible: root.scrubbing
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: backgroundTrack.width * root.previewRatio
            color: root.progressColor
            opacity: 0.95
        }

        // Маркер целевой позиции
        Rectangle {
            visible: root.scrubbing
            x: Math.max(0, Math.min(parent.width - width, root.pointerX - width / 2))
            anchors.bottom: parent.bottom
            anchors.top: parent.top
            width: 2
            color: "#ffffff"
            opacity: 0.9
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        function seekAt(x) {
            if (!root.player || root.duration <= 0)
                return
            root.pointerX = x
            var ratio = Math.max(0, Math.min(1, x / width))
            root.player.seek(ratio * root.duration)
        }

        onContainsMouseChanged: {
            root.hoverChanged(containsMouse)
            if (!containsMouse && !pressed)
                root.pointerX = -1
        }
        onPressed: function(mouse) {
            root.dragStartSeconds = root.player ? root.player.position : 0
            root.interactionStarted()
            seekAt(mouse.x)
            root.pushPreview(true)
        }
        onPositionChanged: function(mouse) {
            // Всегда отслеживаем курсор — превью на таймлайне (и попап кадра)
            // идёт из-под курсора. Seek — только при зажатой кнопке, чтобы
            // простое наведение не перематывало основной плеер.
            root.pointerX = mouse.x
            if (pressed)
                seekAt(mouse.x)
        }
        onReleased: {
            root.pointerX = -1
            root.pushPreview(false)
            root.interactionEnded()
        }
        onWheel: function(wheel) { root.wheel(wheel) }
    }
}