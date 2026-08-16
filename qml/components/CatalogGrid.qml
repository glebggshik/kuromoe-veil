import QtQuick

// Виртуализированная сетка карточек (GridView). Родитель — сам скролл-контейнер.
GridView {
    id: root

    signal cardClicked(var item)
    signal genreClicked(int genreId)
    signal yearClicked(int year)

    property int columns: 4
    property int slot: 166
    property int cardHeight: 230
    property bool enableGenreYear: false

    readonly property int cellH: cardHeight + 6

    model: []
    cellWidth: slot
    cellHeight: cellH
    // 10 рядов оффскрина × декодированные QImage = сотни МБ; 3 достаточно.
    cacheBuffer: cellH * 3
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    reuseItems: true

    delegate: Item {
        width: root.cellWidth
        height: root.cellHeight

        Card {
            anchors.centerIn: parent
            anime: modelData
            onClicked: root.cardClicked(modelData)
            onGenreClicked: function(genreId) {
                if (root.enableGenreYear)
                    root.genreClicked(genreId)
            }
            onYearClicked: function(year) {
                if (root.enableGenreYear)
                    root.yearClicked(year)
            }
        }
    }
}