import QtQuick

Rectangle {
    id: root
    width: 130
    height: 210
    radius: Theme.corner
    color: Theme.bgCard
    clip: true

    property var anime: ({})
    property bool postersEnabled: true
    signal clicked()

    function posterOk(u) {
        return u.length > 0 && !u.endsWith(".webp") && u.indexOf("/missing_") < 0
    }

    readonly property string posterSource: {
        var hd = anime.posterHd || ""
        var p = anime.poster || ""
        if (posterOk(hd)) return hd
        if (posterOk(p)) return p
        return hd || p
    }

    property string loadedFile: ""

    readonly property bool showPoster: root.loadedFile.length > 0
        && poster.status === Image.Ready

    readonly property string placeholderLetter: {
        var t = (anime.title || "?").trim()
        return t.length > 0 ? t.charAt(0).toUpperCase() : "?"
    }

    function refreshPoster() {
        loadedFile = ""
        if (!root.postersEnabled || root.posterSource.length === 0)
            return
        var cached = posterCache.cachedFile(root.posterSource)
        if (cached.length > 0) {
            loadedFile = cached
            return
        }
        if (posterCache.isRemotePoster(root.posterSource))
            posterCache.request(root.posterSource)
    }

    Connections {
        target: posterCache
        function onPosterReady(remoteUrl, fileUrl) {
            if (remoteUrl === root.posterSource)
                root.loadedFile = fileUrl
        }
    }

    onPostersEnabledChanged: refreshPoster()
    onPosterSourceChanged: refreshPoster()
    Component.onCompleted: refreshPoster()

    Rectangle {
        anchors.fill: parent
        visible: !root.showPoster
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2a2a3d" }
            GradientStop { position: 1.0; color: "#1a1a28" }
        }

        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -18
            text: root.placeholderLetter
            color: "#55ffffff"
            font.pixelSize: 42
            font.bold: true
        }
    }

    Image {
        id: poster
        anchors.fill: parent
        source: root.loadedFile
        sourceSize: Qt.size(root.width, root.height)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        cache: true
        opacity: status === Image.Ready ? 1.0 : 0.0
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.6
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: "#ee000000" }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        radius: 8
        color: "#cc1a1a2e"
        width: epRow.width + 12
        height: 20

        Row {
            id: epRow
            anchors.centerIn: parent
            spacing: 4
            Text {
                text: "эп. " + (anime.nextEpisode || "?")
                color: Theme.accentLight
                font.pixelSize: 10
                font.bold: true
            }
            Text {
                visible: (anime.airTime || "").length > 0
                text: anime.airTime || ""
                color: "#d0d0d5"
                font.pixelSize: 10
            }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 2

        Text {
            width: parent.width
            text: anime.title || "?"
            color: "white"
            font.pixelSize: 11
            font.bold: true
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: hoverArea.containsMouse ? 2 : 0
        border.color: Theme.accent
        Behavior on border.width { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}