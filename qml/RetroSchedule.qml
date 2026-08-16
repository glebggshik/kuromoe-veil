import QtQuick
import AnimeClient 1.0

// Порт вкладки "Расписание" из BrowseView.qml/CalendarSection.qml — тут
// отдельный пункт навигации (в обычной теме это вкладка внутри Browse), но
// данные и логика те же: CatalogBridge.loadCalendar().
Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    property bool tabActive: true
    signal openAnime(var item)

    property var days: []
    property bool loading: false
    property bool loadDone: false

    readonly property int totalCount: {
        var n = 0
        for (var i = 0; i < root.days.length; ++i)
            n += (root.days[i].items || []).length
        return n
    }

    CatalogBridge {
        id: bridge
        onCalendarLoadStarted: root.loading = true
        onCalendarReady: function(d) {
            root.days = d
            root.loading = false
        }
        onError: function(msg) {
            console.warn("Retro Schedule:", msg)
            root.loading = false
        }
    }

    function ensureLoaded() {
        if (root.loadDone)
            return
        root.loadDone = true
        bridge.loadCalendar()
    }

    Component.onCompleted: ensureLoaded()

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
            bottomPadding: 4

            Text {
                text: "SCHEDULE"
                font.family: RetroTheme.fontFamily
                font.bold: true
                font.pixelSize: 20
                color: RetroTheme.primary
            }
            Text {
                text: root.loading ? "// loading schedule..." : "// cron -l — " + root.totalCount + " titles airing"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        Repeater {
            model: root.days

            delegate: Column {
                id: dayColumn
                required property var modelData
                x: 20
                width: parent.width - 40
                spacing: 8
                visible: (dayColumn.modelData.items || []).length > 0

                Row {
                    spacing: 8
                    Rectangle { width: 3; height: 12; color: RetroTheme.accent; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: (dayColumn.modelData.label || "").toUpperCase()
                        font.family: RetroTheme.fontFamily
                        font.bold: true
                        font.pixelSize: 12
                        color: RetroTheme.foreground
                    }
                    Text {
                        text: "[" + (dayColumn.modelData.items || []).length + "]"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 11
                        color: RetroTheme.mutedForeground
                    }
                }
                Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

                Column {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: dayColumn.modelData.items || []

                        delegate: Rectangle {
                            id: rowRoot
                            required property var modelData
                            width: parent.width
                            height: 40
                            color: rowMouse.containsMouse ? RetroTheme.card : "transparent"
                            border.width: 1
                            border.color: rowMouse.containsMouse ? RetroTheme.primary : RetroTheme.border

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Text {
                                    width: parent.width - metaText.width - 10
                                    text: rowRoot.modelData.title || "?"
                                    color: rowMouse.containsMouse ? RetroTheme.primary : RetroTheme.foreground
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    id: metaText
                                    text: "EP " + (rowRoot.modelData.nextEpisode || "?")
                                        + ((rowRoot.modelData.airTime || "").length > 0 ? "  " + rowRoot.modelData.airTime : "")
                                    color: RetroTheme.accent
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openAnime(rowRoot.modelData)
                            }
                        }
                    }
                }
            }
        }

        Text {
            x: 20
            visible: !root.loading && root.totalCount === 0
            text: "// no schedule data"
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            color: RetroTheme.mutedForeground
        }
    }
}
