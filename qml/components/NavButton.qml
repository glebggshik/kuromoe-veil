import QtQuick

Item {
    id: root

    property string iconSource: ""
    property string text: ""
    property bool isActive: false
    property int iconSize: 22

    signal clicked()

    implicitWidth: root.iconSize + 16
    implicitHeight: root.iconSize + 16

    Image {
        anchors.centerIn: parent
        source: root.iconSource
        width: root.iconSize
        height: root.iconSize
        sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: root.isActive ? 1 : 0.5

        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}