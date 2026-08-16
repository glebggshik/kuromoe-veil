import QtQuick

// Один вертикальный ListView — стабильно в MSVC Debug, можно листать все дни.
Item {
    id: root

    property var days: []
    property int revision: 0
    property int posterEpoch: 0
    property bool loading: false
    property alias listView: list

    signal openDetail(var item)

    // Индексы вместо копии anime — enrich обновляет days[].items без пересборки списка.
    readonly property var flatEntries: {
        var _rev = root.revision
        var out = []
        for (var d = 0; d < root.days.length; ++d) {
            var day = root.days[d]
            var items = day.items || []
            out.push({
                type: "header",
                label: day.label || "",
                count: items.length
            })
            for (var i = 0; i < items.length; ++i) {
                out.push({
                    type: "item",
                    dayIndex: d,
                    itemIndex: i
                })
            }
        }
        return out
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 4
        clip: true
        spacing: 6
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 240
        model: root.flatEntries

        header: Item {
            width: list.width
            height: root.loading ? 28 : 0
            Text {
                visible: root.loading
                anchors.centerIn: parent
                text: "Загрузка расписания..."
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }

        delegate: Item {
            required property var modelData
            required property int index

            width: list.width
            height: modelData.type === "header" ? 34 : 48

            Row {
                visible: modelData.type === "header"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Rectangle {
                    width: 3
                    height: 14
                    radius: 1.5
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: modelData.label || ""
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: (modelData.count || 0) + " тайтлов"
                    color: Theme.textMuted
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            readonly property var rowAnime: {
                var _epoch = root.posterEpoch
                if (modelData.type !== "item")
                    return {}
                var day = root.days[modelData.dayIndex]
                if (!day)
                    return {}
                var items = day.items || []
                return items[modelData.itemIndex] || {}
            }

            CalendarRow {
                visible: modelData.type === "item"
                anchors.fill: parent
                anime: parent.rowAnime
                onClicked: root.openDetail(parent.rowAnime)
            }
        }
    }
}