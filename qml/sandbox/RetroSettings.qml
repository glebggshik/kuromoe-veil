import QtQuick

Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    readonly property var toggles: [
        { key: "CRT_SCANLINES", label: "CRT scanline overlay", on: true },
        { key: "PHOSPHOR_GLOW", label: "Phosphor text glow", on: true },
        { key: "AUTOPLAY_NEXT", label: "Autoplay next episode", on: true },
        { key: "PREFER_DUB", label: "Prefer dubbed audio", on: false },
        { key: "REDUCE_MOTION", label: "Reduce motion", on: false }
    ]

    property var toggleState: ({})
    Component.onCompleted: {
        var s = {}
        for (var i = 0; i < toggles.length; ++i)
            s[toggles[i].key] = toggles[i].on
        toggleState = s
    }
    function isOn(key) { return !!toggleState[key] }
    function flip(key) {
        var s = toggleState
        s[key] = !s[key]
        toggleState = s
    }

    Column {
        id: contentColumn
        width: root.width
        topPadding: 20
        bottomPadding: 24
        spacing: 20

        Column {
            x: 20
            width: parent.width - 40
            spacing: 4
            bottomPadding: 8

            Text {
                text: "SETTINGS"
                font.family: RetroTheme.fontFamily
                font.bold: true
                font.pixelSize: 20
                color: RetroTheme.primary
            }
            Text {
                text: "// ~/.config/terminal.watch/prefs.cfg"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 0

            Rectangle {
                width: parent.width
                height: 1
                color: "transparent"
            }

            Rectangle {
                width: parent.width
                height: toggleColumn.height
                color: RetroTheme.card
                border.width: 1
                border.color: RetroTheme.border

                Column {
                    id: toggleColumn
                    width: parent.width

                    Repeater {
                        model: root.toggles
                        delegate: Item {
                            width: toggleColumn.width
                            height: 54

                            Rectangle {
                                visible: index !== root.toggles.length - 1
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: RetroTheme.border
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 16
                                spacing: 2
                                Text {
                                    text: modelData.label
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 12
                                    color: RetroTheme.foreground
                                }
                                Text {
                                    text: modelData.key
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 9
                                    color: RetroTheme.mutedForeground
                                }
                            }

                            Rectangle {
                                readonly property bool on: root.isOn(modelData.key)
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.rightMargin: 16
                                width: 56; height: 24
                                color: on ? Qt.rgba(0.220, 0.973, 0.573, 0.2) : RetroTheme.muted
                                border.width: 1
                                border.color: on ? RetroTheme.primary : RetroTheme.border

                                Rectangle {
                                    x: parent.on ? parent.width - width - 2 : 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 24; height: 16
                                    color: parent.on ? RetroTheme.primary : Qt.rgba(0.435, 0.506, 0.447, 0.4)
                                    Behavior on x { NumberAnimation { duration: 120 } }
                                    Text {
                                        anchors.centerIn: parent
                                        text: parent.parent.on ? "ON" : "OFF"
                                        font.family: RetroTheme.fontFamily
                                        font.pixelSize: 7
                                        color: parent.parent.on ? RetroTheme.primaryForeground : RetroTheme.background
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.flip(modelData.key)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
