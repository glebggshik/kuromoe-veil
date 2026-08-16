import QtQuick
import AnimeClient 1.0
import QtQuick.Effects

// Порт RetroSelect из detail-screen.tsx: кастомный dropdown, ">" перед
// значением, список раскрывается снизу поверх остального контента.
Column {
    id: root
    spacing: 6

    property string label: ""
    property var options: []
    property string value: options.length > 0 ? options[0] : ""
    signal changed(string newValue)

    property bool open: false

    Text {
        text: root.label.toUpperCase()
        font.family: RetroTheme.fontFamily
        font.pixelSize: 9
        color: RetroTheme.mutedForeground
    }

    Rectangle {
        id: box
        width: parent.width
        height: 40
        color: RetroTheme.card
        border.width: 1
        border.color: root.open ? RetroTheme.primary : (boxMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.6) : RetroTheme.border)

        layer.enabled: root.open
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: RetroTheme.primary
            shadowBlur: 0.5
            shadowOpacity: 0.4
        }

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            spacing: 6
            Text { text: ">"; color: RetroTheme.primary; font.family: RetroTheme.fontFamily; font.pixelSize: 12 }
            Text {
                text: root.value
                color: root.open ? RetroTheme.primary : RetroTheme.foreground
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
            }
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 12
            text: root.open ? "▲" : "▼"
            font.pixelSize: 9
            color: root.open ? RetroTheme.primary : RetroTheme.mutedForeground
        }

        MouseArea {
            id: boxMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.open = !root.open
        }
    }

    // Выпадающий список — поверх остального контента.
    Rectangle {
        id: dropdown
        parent: box
        anchors.left: box.left
        anchors.right: box.right
        anchors.top: box.bottom
        anchors.topMargin: 2
        z: 100
        height: visible ? optColumn.height : 0
        visible: root.open
        color: RetroTheme.card
        border.width: 1
        border.color: RetroTheme.primary

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: RetroTheme.primary
            shadowBlur: 0.5
            shadowOpacity: 0.4
        }

        Column {
            id: optColumn
            width: parent.width

            Repeater {
                model: root.options
                delegate: Rectangle {
                    readonly property bool selected: modelData === root.value
                    width: optColumn.width
                    height: 32
                    color: selected ? Qt.rgba(0.220, 0.973, 0.573, 0.15) : (optMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.08) : "transparent")

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        text: modelData
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        color: selected ? RetroTheme.primary : RetroTheme.mutedForeground
                    }
                    Text {
                        visible: selected
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 12
                        text: "✓"
                        font.pixelSize: 11
                        color: RetroTheme.primary
                    }
                    MouseArea {
                        id: optMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.value = modelData
                            root.changed(modelData)
                            root.open = false
                        }
                    }
                }
            }
        }
    }
}
