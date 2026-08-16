import QtQuick
import QtQuick.Controls
import AnimeClient 1.0
import "components"

Item {
    id: root
    property bool tabActive: false
    signal openDetail(var item)

    readonly property var tabs: [
        { value: "watching", label: "Смотрю" },
        { value: "planned", label: "В планах" },
        { value: "watched", label: "Просмотрено" },
        { value: "postponed", label: "Отложено" },
        { value: "dropped", label: "Брошено" }
    ]
    property string currentTab: "watching"
    property var cardsModel: []
    property bool initialLoadDone: false
    property bool dataReady: false
    // Фоновый прогрев декода, пока пользователь на другой вкладке — без фриза при переходе.
    property bool warmPosters: false

    readonly property int slot: 166
    readonly property int gridColumns: Math.max(1, Math.floor((width - 32) / slot))
    readonly property int contentWidth: Math.min(width - 32, gridColumns * slot)

    BookmarksBridge {
        id: bridge
        onResultsReady: function(items) {
            // Порядок важен — см. комментарий в BrowseView.qml: модель ставим
            // до dataReady, иначе сплэш закрывается раньше, чем реально
            // создались делегаты карточек, и фриз переезжает на первый кадр
            // после перехода на вкладку вместо того, чтобы быть скрытым сплэшем.
            root.cardsModel = items
            posterCache.preloadCatalog(items)
            if (!root.tabActive)
                root.warmPosters = true
            Qt.callLater(function() { root.dataReady = true })
        }
        onError: function(msg) {
            root.dataReady = true
            console.warn("Закладки:", msg)
        }
    }

    function ensureLoaded() {
        if (root.initialLoadDone)
            return
        root.initialLoadDone = true
        bridge.loadStatus(root.currentTab)
    }

    function reload() {
        bridge.loadStatus(root.currentTab)
    }

    onTabActiveChanged: {
        if (root.tabActive)
            root.warmPosters = false
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width
            spacing: 0

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: 20
                bottomPadding: 16
                spacing: 8
                Repeater {
                    model: root.tabs
                    delegate: PillButton {
                        text: modelData.label
                        active: root.currentTab === modelData.value
                        onClicked: {
                            root.currentTab = modelData.value
                            root.reload()
                        }
                    }
                }
            }

            GridView {
                id: cardGrid
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.contentWidth
                height: {
                    var cols = Math.max(1, Math.floor(width / root.slot))
                    var rows = Math.ceil(count / cols)
                    return rows > 0 ? rows * (230 + 6) : 0
                }
                interactive: false
                cellWidth: root.slot
                cellHeight: 230 + 6
                flow: GridView.FlowLeftToRight
                model: root.cardsModel
                cacheBuffer: 800
                delegate: Card {
                    anime: modelData
                    imagesEnabled: root.tabActive || root.warmPosters
                    onClicked: root.openDetail(modelData)
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: 40
                visible: root.cardsModel.length === 0
                text: "Пока пусто"
                color: Theme.textMuted
                font.pixelSize: 14
            }

            Item { width: 1; height: 24 }
        }
    }
}