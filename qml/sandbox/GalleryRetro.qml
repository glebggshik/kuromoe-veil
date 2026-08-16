import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: win
    width: 1180
    height: 780
    visible: true
    title: "Sandbox — Retro Terminal"
    color: RetroTheme.background

    RetroShell {
        anchors.fill: parent
    }
}
