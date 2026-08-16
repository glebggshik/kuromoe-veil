import QtQuick

// Заглушка вместо C++ PosterThumbnail (её нельзя запустить в чистом qml.exe —
// требует Qt-модуль AnimeClient, зарегистрированный из main.cpp). Тот же
// список свойств, тот же fallback-вид (градиент + буква), что и в реальном
// PosterThumbnail::paint(), когда постер не загружен — этого достаточно,
// чтобы оценивать вёрстку/цвета/типографику карточки без реальных картинок.
Rectangle {
    id: root
    property string posterSource: ""
    property bool posterActive: true
    property real cornerRadius: 10
    property string placeholderLetter: "?"

    radius: cornerRadius
    clip: true
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#2a2a3d" }
        GradientStop { position: 1.0; color: "#1a1a28" }
    }

    Text {
        anchors.centerIn: parent
        visible: root.placeholderLetter.length > 0
        text: root.placeholderLetter
        color: "#55ffffff"
        font.bold: true
        font.pixelSize: Math.max(18, Math.min(parent.width, parent.height) * 0.35)
    }
}
