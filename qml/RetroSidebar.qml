import QtQuick
import AnimeClient 1.0
import QtQuick.Effects

// Порт components/sidebar.tsx: вертикальная навигация 64px, иконка+подпись,
// активный пункт — левая зелёная полоска + рамка + свечение (box-glow).
Rectangle {
    id: root
    width: 68
    color: RetroTheme.sidebar
    border.width: 0

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: RetroTheme.border }

    property string active: "home"
    signal navigate(string screen)

    readonly property var navItems: [
        { id: "home", label: "HOME", icon: "home.svg" },
        { id: "browse", label: "BROWSE", icon: "compass.svg" },
        { id: "schedule", label: "SCHED", icon: "calendar.svg" },
        { id: "bookmarks", label: "MARKS", icon: "bookmark.svg" },
        { id: "random", label: "RANDOM", icon: "dice.svg" },
        { id: "settings", label: "CONFIG", icon: "settings.svg" }
    ]

    Column {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 16
        spacing: 8

        Repeater {
            model: root.navItems
            delegate: Item {
                width: 48; height: 48
                readonly property bool isActive: root.active === modelData.id

                Rectangle {
                    anchors.fill: parent
                    color: isActive ? Qt.rgba(0.220, 0.973, 0.573, 0.15) : (btnMouse.containsMouse ? RetroTheme.sidebarAccent : "transparent")
                    border.width: 1
                    border.color: isActive ? RetroTheme.primary : "transparent"

                    layer.enabled: isActive
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: RetroTheme.primary
                        shadowBlur: 0.5
                        shadowOpacity: 0.4
                    }
                }
                // Активная полоска слева
                Rectangle {
                    visible: isActive
                    anchors.left: parent.left
                    width: 2; height: parent.height
                    color: RetroTheme.primary
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 3
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: Qt.resolvedUrl("assets/" + modelData.icon)
                        width: 17; height: 17
                        sourceSize: Qt.size(34, 34)
                        fillMode: Image.PreserveAspectFit
                        smooth: true

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: isActive ? RetroTheme.primary : RetroTheme.sidebarForeground
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 7
                        color: isActive ? RetroTheme.primary : RetroTheme.sidebarForeground
                    }
                }

                MouseArea {
                    id: btnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.navigate(modelData.id)
                }
            }
        }
    }

    // Power — прижат к низу
    Item {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 12
        width: 48; height: 48

        Rectangle {
            anchors.fill: parent
            color: powerMouse.containsMouse ? Qt.rgba(1, 0.353, 0.302, 0.15) : "transparent"
            border.width: 1
            border.color: powerMouse.containsMouse ? RetroTheme.destructive : "transparent"
        }
        Text {
            anchors.centerIn: parent
            text: "⏻"
            font.pixelSize: 16
            color: powerMouse.containsMouse ? RetroTheme.destructive : RetroTheme.mutedForeground
        }
        MouseArea {
            id: powerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Qt.quit()
        }
    }
}
