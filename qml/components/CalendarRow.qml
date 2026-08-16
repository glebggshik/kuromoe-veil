import QtQuick

Rectangle {
    id: root
    height: 48
    radius: Theme.cornerSmall
    color: mouse.containsMouse ? Theme.bgCardHover : Theme.bgCard

    property var anime: ({})
    signal clicked()

    Row {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Text {
            width: parent.width - meta.implicitWidth - 10
            text: anime.title || "?"
            color: Theme.textPrimary
            font.pixelSize: 13
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: meta
            text: "эп. " + (anime.nextEpisode || "?")
                + ((anime.airTime || "").length > 0 ? "  " + anime.airTime : "")
            color: Theme.accentLight
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}