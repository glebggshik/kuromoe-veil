import QtQuick

// Порт components/title-bar.tsx: три индикатора-квадрата, лейбл "TERMINAL.WATCH"
// со свечением, живые часы, три кнопки окна (свернуть/развернуть/закрыть).
Rectangle {
    id: root
    height: 36
    color: RetroTheme.card
    border.width: 0

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: RetroTheme.border }

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
            WinBtn { glyph: "—" }
            WinBtn { glyph: "□" }
            WinBtn { glyph: "×"; danger: true }
        }
    }

    component WinBtn: Rectangle {
        property string glyph: ""
        property bool danger: false
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
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
    }
}
