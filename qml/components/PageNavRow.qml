import QtQuick

// Пагинация у сетки тайтлов: круглые chevron слева/справа от номера страницы.
Row {
    id: root

    property int page: 1
    property bool hasPrev: false
    property bool hasNext: false

    signal prevClicked()
    signal nextClicked()

    spacing: 12
    topPadding: 8
    bottomPadding: 14

    Rectangle {
        width: 36
        height: 36
        radius: 18
        color: "#1C1C1E"
        opacity: root.hasPrev ? 1 : 0.35

        Image {
            anchors.centerIn: parent
            width: 18
            height: 18
            source: Qt.resolvedUrl("../assets/chevron-left.svg")
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.hasPrev
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: root.prevClicked()
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: "Страница " + root.page
        color: Theme.textSecondary
        font.pixelSize: 12
    }

    Rectangle {
        width: 36
        height: 36
        radius: 18
        color: "#1C1C1E"
        opacity: root.hasNext ? 1 : 0.35

        Image {
            anchors.centerIn: parent
            width: 18
            height: 18
            source: Qt.resolvedUrl("../assets/chevron-right.svg")
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.hasNext
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: root.nextClicked()
        }
    }
}