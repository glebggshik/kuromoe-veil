import QtQuick
import AnimeClient 1.0

Item {
    id: root

    property string iconSource: ""
    property bool closeButton: false
    property bool minimizeLine: false
    property bool squareOutline: false

    readonly property real iconOpacity: mouseArea.containsMouse ? 1.0 : 0.92

    signal clicked()

    width: 46
    height: 38

    Rectangle {
        id: bg
        anchors.fill: parent
        color: root.closeButton && mouseArea.containsMouse
            ? "#E81123"
            : (mouseArea.containsMouse ? Theme.bgCardHover : "transparent")
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    Rectangle {
        anchors.centerIn: parent
        visible: root.minimizeLine
        width: 10
        height: 1
        color: "#ffffff"
        opacity: root.iconOpacity
        Behavior on opacity { NumberAnimation { duration: 90 } }
    }

    Rectangle {
        anchors.centerIn: parent
        visible: root.squareOutline
        width: 10
        height: 10
        color: "transparent"
        border.color: "#ffffff"
        border.width: 1
        opacity: root.iconOpacity
        Behavior on opacity { NumberAnimation { duration: 90 } }
    }

    Image {
        anchors.centerIn: parent
        visible: !root.minimizeLine && !root.squareOutline && root.iconSource !== ""
        source: root.iconSource
        width: 14
        height: 14
        sourceSize: Qt.size(96, 96)
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: root.iconOpacity
        Behavior on opacity { NumberAnimation { duration: 90 } }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}