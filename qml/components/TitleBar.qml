import QtQuick
import QtQuick.Window
import AnimeClient 1.0

// Кастомный заголовок вместо системной рамки Windows.
Item {
    id: root

    required property Window window

    readonly property bool maximized: root.window.visibility === Window.Maximized
        || root.window.visibility === Window.FullScreen

    height: 38
    z: 30

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSidebar

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.bgPill
            opacity: 0.65
        }
    }

    Row {
        id: brandRow
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Image {
            source: Qt.resolvedUrl("../assets/logo.svg")
            width: 22
            height: 22
            sourceSize: Qt.size(44, 44)
            fillMode: Image.PreserveAspectFit
            smooth: true
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: "KuroMoe Veil"
            color: Theme.textPrimary
            font.pixelSize: 13
            font.bold: true
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Row {
        id: controlsRow
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        TitleBarButton {
            minimizeLine: true
            onClicked: root.window.showMinimized()
        }

        TitleBarButton {
            squareOutline: !root.maximized
            iconSource: root.maximized
                ? Qt.resolvedUrl("../assets/window/restore.png")
                : ""
            onClicked: {
                if (root.maximized)
                    root.window.showNormal()
                else
                    root.window.showMaximized()
            }
        }

        TitleBarButton {
            iconSource: Qt.resolvedUrl("../assets/window/close.png")
            closeButton: true
            onClicked: root.window.close()
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: controlsRow.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        acceptedButtons: Qt.LeftButton
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton && root.window.startSystemMove)
                root.window.startSystemMove()
        }
        onDoubleClicked: {
            if (root.maximized)
                root.window.showNormal()
            else
                root.window.showMaximized()
        }
    }
}