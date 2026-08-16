import QtQuick
import AnimeClient 1.0

// Экран загрузки для retro-темы — зелёный терминал с "bash:"-приглашением
// вместо плавного логотипа обычной SplashScreen.qml. Строки лога печатаются
// по одной (эффект набора команд), внизу — блочный ASCII-прогрессбар и
// мигающий курсор.
//
// ВАЖНО (баг, на котором словили мигание всего окна раз в ~секунду):
// раньше курсор мигал через SequentialAnimation с loops: Animation.Infinite
// и running: true намертво, а сам сплэш после dismiss() просто прятался
// (visible: false), но не уничтожался. Невидимый item в Qt Quick не
// перестаёт анимироваться — а активная анимация ГДЕ УГОДНО в окне держит
// рендер-луп в режиме постоянной перерисовки ВСЕГО окна, даже если сам
// анимированный элемент не виден и ничего физически не меняет на экране.
// Отсюда и мигание всего приложения бесконечно после закрытия загрузки.
// Фикс: (1) running анимации курсора привязан к root.visible, и (2) сам
// компонент грузится через Loader и полностью выгружается (active: false)
// после dismiss — не просто прячется.
Rectangle {
    id: root
    anchors.fill: parent
    color: RetroTheme.background
    z: 100

    signal dismissed()

    readonly property var bootLines: [
        "mounting /dev/shikimori0...",
        "mounting /dev/kodik0...",
        "loading catalog modules...",
        "decrypting phosphor cache...",
        "spawning ui thread... OK"
    ]
    property int linesShown: 0

    ScanlinesOverlay { anchors.fill: parent }

    Column {
        anchors.centerIn: parent
        spacing: 26
        width: Math.min(420, root.width - 80)

        Item {
            width: 132
            height: 132
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                id: logo
                anchors.centerIn: parent
                width: 120
                height: 120
                source: Qt.resolvedUrl("assets/logo-retro.svg")
                sourceSize: Qt.size(240, 240)
                fillMode: Image.PreserveAspectFit
                smooth: true
                opacity: 0
                scale: 0.92

                SequentialAnimation {
                    running: true
                    PauseAnimation { duration: 80 }
                    ParallelAnimation {
                        NumberAnimation { target: logo; property: "opacity"; to: 1; duration: 420; easing.type: Easing.OutCubic }
                        NumberAnimation { target: logo; property: "scale"; to: 1.0; duration: 520; easing.type: Easing.OutBack; easing.amplitude: 0.9 }
                    }
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "KUROMOE VEIL"
            font.family: RetroTheme.fontFamily
            font.bold: true
            font.pixelSize: 20
            font.letterSpacing: 4
            color: RetroTheme.primary
        }

        // === Терминал с bash-приглашением ===
        Rectangle {
            width: parent.width
            height: termColumn.height + 24
            color: RetroTheme.card
            border.width: 1
            border.color: RetroTheme.border

            Column {
                id: termColumn
                x: 16
                y: 12
                width: parent.width - 32
                spacing: 5

                Repeater {
                    model: root.linesShown
                    delegate: Row {
                        spacing: 8
                        Text {
                            text: "bash:"
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 11
                            color: RetroTheme.mutedForeground
                        }
                        Text {
                            text: "$ " + root.bootLines[index]
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 11
                            color: index === root.linesShown - 1 ? RetroTheme.primary : RetroTheme.foreground
                        }
                    }
                }

                Row {
                    spacing: 8
                    visible: root.linesShown < root.bootLines.length
                    Text {
                        text: "bash:"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        color: RetroTheme.mutedForeground
                    }
                    Text {
                        text: "$"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        color: RetroTheme.primary
                    }
                    Rectangle {
                        id: cursor
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7; height: 13
                        color: RetroTheme.primary

                        SequentialAnimation {
                            loops: Animation.Infinite
                            // Привязано к видимости сплэша — не крутится вечно в фоне
                            // после dismiss() (см. комментарий в шапке файла).
                            running: root.visible
                            NumberAnimation { target: cursor; property: "opacity"; from: 1; to: 0; duration: 420 }
                            NumberAnimation { target: cursor; property: "opacity"; from: 0; to: 1; duration: 420 }
                        }
                    }
                }
            }

            Timer {
                interval: 220
                repeat: true
                running: root.visible && root.linesShown < root.bootLines.length
                onTriggered: root.linesShown += 1
            }
        }

        // === Блочный прогрессбар ===
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 3

            Repeater {
                model: 24
                delegate: Rectangle {
                    readonly property bool filled: index < progressFill.value
                    width: 10; height: 14
                    color: filled ? RetroTheme.primary : "transparent"
                    border.width: filled ? 0 : 1
                    border.color: RetroTheme.border
                }
            }
        }
    }

    QtObject {
        id: progressFill
        property real value: 0

        NumberAnimation on value {
            running: root.visible
            from: 0
            to: 24
            duration: 1400
            easing.type: Easing.InOutQuad
        }
    }

    opacity: 1

    function dismiss() {
        if (fadeOut.running)
            return
        fadeOut.start()
    }

    // Одноразовая анимация (не Infinite) — сама останавливается по завершении,
    // это не источник бесконечной перерисовки. Настоящую выгрузку делает
    // MainRetro.qml через Loader.active = false в onFinished.
    NumberAnimation {
        id: fadeOut
        target: root
        property: "opacity"
        to: 0
        duration: 380
        easing.type: Easing.OutCubic
        onFinished: root.dismissed()
    }
}
