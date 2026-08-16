import QtQuick
import "retroData.js" as RetroData

Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    signal openAnime(string id)

    readonly property var allItems: RetroData.items()

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
                text: "BROWSE"
                font.family: RetroTheme.fontFamily
                font.bold: true
                font.pixelSize: 20
                color: RetroTheme.primary
            }
            Text {
                text: "// ls -la /catalog — " + root.allItems.length + " titles found"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        Flow {
            x: 20
            width: parent.width - 40
            spacing: 12

            Repeater {
                model: root.allItems
                delegate: RetroCard {
                    width: 168
                    anime: modelData
                    onClicked: root.openAnime(modelData.id)
                }
            }
        }
    }
}
