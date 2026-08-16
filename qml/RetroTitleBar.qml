import QtQuick
import AnimeClient 1.0
import QtQuick.Window

// Порт components/title-bar.tsx: три индикатора-квадрата, лейбл "TERMINAL.WATCH"
// со свечением, живые часы, три кнопки окна (свернуть/развернуть/закрыть).
// window необязателен (не required) — компонент должен запускаться и
// standalone через qml.exe в песочнице, где окна снаружи не передают.
Rectangle {
    id: root
    property Window window: null
    height: 36
    color: RetroTheme.card
    border.width: 0

    readonly property bool maximized: root.window
        && (root.window.fakeMaximized === true || root.window.visibility === Window.FullScreen)

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: RetroTheme.border }

    // Перетаскивание окна за пустую область заголовка (не задевая кнопки/часы).
    MouseArea {
        anchors.left: parent.left
        anchors.right: winButtonsRow.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        acceptedButtons: Qt.LeftButton
        onPressed: function(mouse) {
            if (root.window && root.window.startSystemMove)
                root.window.startSystemMove()
        }
        onDoubleClicked: {
            if (!root.window) return
            // toggleMaximize() (MainRetro.qml) — ручная геометрия на весь экран
            // вместо native showMaximized()/visibility=Maximized. И то, и другое
            // у фреймлесс-окна на Windows иногда даёт кадр со старым размером
            // контента поверх уже увеличенного чёрного фона во время системной
            // анимации разворота — ручная геометрия разворачивает мгновенно,
            // без этой анимации.
            root.window.toggleMaximize()
        }
    }

    property string clockText: "--:--:--"
    Timer {
        interval: 1000; running: true; repeat: true
        triggeredOnStart: true
        onTriggered: root.clockText = Qt.formatTime(new Date(), "hh:mm:ss")
    }

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        spacing: 12

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Rectangle { width: 10; height: 10; color: Qt.rgba(0.220, 0.973, 0.573, 0.4); border.width: 1; border.color: RetroTheme.primary }
            Rectangle { width: 10; height: 10; color: Qt.rgba(1, 0.702, 0.169, 0.4); border.width: 1; border.color: RetroTheme.accent }
            Rectangle { width: 10; height: 10; color: "transparent"; border.width: 1; border.color: Qt.rgba(0.435, 0.506, 0.447, 0.6) }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "TERMINAL.WATCH"
            font.family: RetroTheme.fontFamily
            font.bold: true
            font.pixelSize: 11
            color: RetroTheme.primary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "~/stream/session_01 — 80x24"
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            color: RetroTheme.mutedForeground
        }
    }

    Row {
        id: winButtonsRow
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 12
        spacing: 16

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.clockText
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            color: RetroTheme.accent
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            WinBtn {
                glyph: "—"
                onClicked: if (root.window) root.window.showMinimized()
            }
            WinBtn {
                glyph: "□"
                onClicked: {
                    if (!root.window) return
                    root.window.toggleMaximize()
                }
            }
            WinBtn {
                glyph: "×"
                danger: true
                onClicked: if (root.window) root.window.close()
            }
        }
    }

    component WinBtn: Rectangle {
        property string glyph: ""
        property bool danger: false
        signal clicked()
        width: 22; height: 22
        color: mouse.containsMouse ? (danger ? RetroTheme.destructive : Qt.rgba(0.220, 0.973, 0.573, 0.15)) : "transparent"
        border.width: 1
        border.color: mouse.containsMouse ? (danger ? RetroTheme.destructive : RetroTheme.primary) : RetroTheme.border

        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.pixelSize: 11
            color: mouse.containsMouse && parent.danger ? RetroTheme.background : RetroTheme.mutedForeground
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }
}
