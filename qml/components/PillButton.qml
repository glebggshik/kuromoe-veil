import QtQuick
import QtQuick.Controls

Button {
    id: root
    property bool active: false
    property string iconSource: ""
    property int iconSize: 15
    implicitHeight: 36
    opacity: enabled ? 1 : 0.45

    readonly property color labelColor: !root.enabled ? Theme.textMuted
        : (root.active ? "white" : Theme.textPrimary)

    background: Rectangle {
        radius: Theme.cornerPill
        color: !root.enabled ? Theme.bgPill
            : (root.active ? Theme.accent : (root.hovered ? Theme.bgCardHover : Theme.bgPill))
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: Row {
        spacing: root.iconSource !== "" ? 6 : 0
        anchors.centerIn: parent
        leftPadding: root.iconSource !== "" ? 12 : 14
        rightPadding: 14

        Image {
            visible: root.iconSource !== ""
            source: root.iconSource
            width: root.iconSize
            height: root.iconSize
            sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: root.active ? 1.0 : 0.82
            anchors.verticalCenter: parent.verticalCenter

            Behavior on opacity { NumberAnimation { duration: 120 } }
        }

        Text {
            text: root.text
            color: root.labelColor
            font.pixelSize: 13
            font.bold: root.active
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}