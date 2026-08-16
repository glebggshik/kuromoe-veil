import QtQuick
import QtQuick.Controls
import "sandboxData.js" as SandboxData

ApplicationWindow {
    width: 900
    height: 700
    visible: true
    title: "Sandbox — Card v2 (альтернативный дизайн)"
    color: Theme.bgApp

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: grid.height
        clip: true

        GridView {
            id: grid
            width: parent.width
            height: Math.ceil(count / Math.max(1, Math.floor(width / 166))) * 256
            cellWidth: 166
            cellHeight: 256
            interactive: false
            model: SandboxData.items()
            delegate: CardV2 {
                anime: modelData
            }
        }
    }
}
