import QtQuick
import QtQuick.Controls

// Кнопка uosc: SVG-иконка или текст, прозрачный фон, лёгкий hover.
Item {
    id: root

    property string icon: ""
    property string iconSource: ""
    property string label: ""
    property bool active: false
    property bool buttonEnabled: true
    property int size: 32
    property color fg: "#ffffff"
    property color fgMuted: "#ffffff88"
    property color bgHover: "#1affffff"

    signal clicked()

    implicitWidth: size
    implicitHeight: size

    readonly property real contentOpacity: !root.buttonEnabled ? 0.45
        : (root.active ? 1 : (mouse.containsMouse ? 1 : 0.88))

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: mouse.containsMouse && root.buttonEnabled ? root.bgHover : "transparent"
        Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }

        Image {
            anchors.centerIn: parent
            visible: root.iconSource !== ""
            source: root.iconSource
            width: Math.round(root.size * 0.56)
            height: width
            sourceSize: Qt.size(width * 2, height * 2)
            fillMode: Image.PreserveAspectFit
            opacity: root.contentOpacity
            Behavior on opacity { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }
        }

        Text {
            anchors.centerIn: parent
            visible: root.iconSource === ""
            text: root.label !== "" ? root.label : root.icon
            color: !root.buttonEnabled ? root.fgMuted
                : (root.active ? root.fg : (mouse.containsMouse ? root.fg : "#ffffffdd"))
            font.pixelSize: root.label !== "" ? 11 : 16
            font.bold: root.label !== ""
            opacity: root.contentOpacity
            Behavior on color { ColorAnimation { duration: 100; easing.type: Easing.OutCubic } }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.buttonEnabled
            onClicked: root.clicked()
        }
    }
}