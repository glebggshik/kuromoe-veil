import QtQuick
import QtQuick.Controls
import "sandboxData.js" as SandboxData

ApplicationWindow {
    width: 900
    height: 700
    visible: true
    title: "Sandbox — Card v1 (текущий дизайн)"
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
            height: Math.ceil(count / Math.max(1, Math.floor(width / 166))) * 236
            cellWidth: 166
            cellHeight: 236
            interactive: false
            model: SandboxData.items()
            delegate: CardV1 {
                anime: modelData
                showAiredEpisode: index % 3 === 0
            }
        }
    }
}
