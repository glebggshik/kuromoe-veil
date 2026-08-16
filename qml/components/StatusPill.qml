import QtQuick
import QtQuick.Controls

Button {
    id: root
    property string label: ""
    property string value: ""
    property string current: ""
    readonly property bool active: current === value
    signal picked(string status)

    text: label
    implicitHeight: 36
    background: Rectangle {
        radius: Theme.cornerPill
        color: root.active ? Theme.accent : (root.hovered ? Theme.bgCardHover : Theme.bgPill)
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Text {
        text: root.label
        color: root.active ? "white" : Theme.textPrimary
        font.pixelSize: 13
        font.bold: root.active
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        leftPadding: 14
        rightPadding: 14
    }
    // повторный клик по уже активному статусу снимает его (пустая строка = "без статуса")
    onClicked: root.picked(root.active ? "" : root.value)
}
