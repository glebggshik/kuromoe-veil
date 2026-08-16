import QtQuick

// Экран загрузки: фон как у приложения, логотип и название по центру.
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgApp
    z: 100

    signal dismissed()

    readonly property string titleFont: {
        var families = Qt.fontFamilies()
        if (families.indexOf("Bahnschrift") >= 0)
            return "Bahnschrift"
        if (families.indexOf("Segoe UI Light") >= 0)
            return "Segoe UI Light"
        if (families.indexOf("Segoe UI") >= 0)
            return "Segoe UI"
        return Qt.application.font.family
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: "#0a0a0f" }
            GradientStop { position: 0.45; color: "#14141c" }
            GradientStop { position: 1.0; color: "#0a0a0f" }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -36
        width: 300
        height: 300
        radius: 150
        color: Theme.accent
        opacity: 0.07
    }

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -20
        spacing: 22

        Item {
            width: 108
            height: 108
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                id: logo
                anchors.centerIn: parent
                width: 96
                height: 96
                source: Qt.resolvedUrl("../assets/logo.svg")
                sourceSize: Qt.size(192, 192)
                fillMode: Image.PreserveAspectFit
                smooth: true
                opacity: 0
                scale: 0.92

                SequentialAnimation {
                    running: true
                    PauseAnimation { duration: 80 }
                    ParallelAnimation {
                        NumberAnimation {
                            target: logo
                            property: "opacity"
                            to: 1
                            duration: 520
                            easing.type: Easing.OutCubic
                        }
                        NumberAnimation {
                            target: logo
                            property: "scale"
                            to: 1.0
                            duration: 620
                            easing.type: Easing.OutBack
                            easing.amplitude: 0.9
                        }
                    }
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "KuroMoe Veil"
            color: Theme.textPrimary
            font.family: root.titleFont
            font.pixelSize: 34
            font.weight: Font.Light
            font.letterSpacing: 1.4
            opacity: 0

            SequentialAnimation on opacity {
                running: true
                PauseAnimation { duration: 200 }
                NumberAnimation { to: 1; duration: 480; easing.type: Easing.OutCubic }
            }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: 120
        height: 3
        radius: 2
        color: "#22ffffff"
        clip: true
        visible: root.opacity > 0.01

        Rectangle {
            id: progressKnob
            width: parent.width * 0.42
            height: parent.height
            radius: 2
            color: Theme.accent
            opacity: 0.85
            x: -width

            SequentialAnimation {
                loops: Animation.Infinite
                running: root.visible
                NumberAnimation {
                    target: progressKnob
                    property: "x"
                    from: -progressKnob.width
                    to: progressKnob.parent.width
                    duration: 1100
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }

    opacity: 1
    visible: opacity > 0.001

    function dismiss() {
        if (fadeOut.running)
            return
        fadeOut.start()
    }

    NumberAnimation {
        id: fadeOut
        target: root
        property: "opacity"
        to: 0
        duration: 420
        easing.type: Easing.OutCubic
        onFinished: root.dismissed()
    }
}