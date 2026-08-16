import QtQuick

// Порт desktop-shell.tsx — общая оболочка со всеми разделами.
// В отличие от оригинала: на экране деталей sidebar НЕ подсвечивает "Home"
// (в оригинале navActive = screen === 'detail' ? 'home' : screen — заметная
// нестыковка на скриншотах, тут сознательно не повторяем).
Item {
    id: root

    property string screen: "home"          // home | browse | bookmarks | settings | detail
    property string selectedAnimeId: "cherry-blade"

    readonly property var paths: ({
        home: "~/home", browse: "~/browse", bookmarks: "~/library",
        settings: "~/config", detail: "~/watch"
    })
    // На detail ничего не подсвечиваем в sidebar (в оригинале был баг: подсвечивался Home)
    readonly property string sidebarActive: root.screen === "detail" ? "" : root.screen

    function navigate(s) { root.screen = s }
    function openAnime(id) {
        root.selectedAnimeId = id
        root.screen = "detail"
    }

    Column {
        anchors.fill: parent
        spacing: 0

        RetroTitleBar {
            width: parent.width
            height: 36
        }

        // === Breadcrumb / статус-строка ===
        Rectangle {
            width: parent.width
            height: 28
            color: RetroTheme.card
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: RetroTheme.border }

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                spacing: 6
                Text { text: "user@terminal.watch"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.primary }
                Text { text: ":"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.mutedForeground }
                Text { text: root.paths[root.screen] || ""; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: RetroTheme.accent }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 16
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 6
                    color: RetroTheme.primary
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 0.25; duration: 700 }
                        NumberAnimation { from: 0.25; to: 1; duration: 700 }
                    }
                }
                Text { text: "CONNECTED"; font.family: RetroTheme.fontFamily; font.pixelSize: 9; color: RetroTheme.mutedForeground }
            }
        }

        Row {
            width: parent.width
            height: parent.height - 36 - 28
            spacing: 0

            RetroSidebar {
                height: parent.height
                active: root.sidebarActive
                onNavigate: function(s) { root.navigate(s) }
            }

            Item {
                width: parent.width - 68
                height: parent.height

                Loader {
                    anchors.fill: parent
                    sourceComponent: {
                        switch (root.screen) {
                        case "browse": return browseComp
                        case "bookmarks": return bookmarksComp
                        case "settings": return settingsComp
                        case "detail": return detailComp
                        default: return homeComp
                        }
                    }
                }
            }
        }
    }

    Component {
        id: homeComp
        RetroHome { onOpenAnime: function(id) { root.openAnime(id) } }
    }
    Component {
        id: browseComp
        RetroBrowse { onOpenAnime: function(id) { root.openAnime(id) } }
    }
    Component {
        id: bookmarksComp
        RetroBookmarks { onOpenAnime: function(id) { root.openAnime(id) } }
    }
    Component {
        id: settingsComp
        RetroSettings {}
    }
    Component {
        id: detailComp
        RetroDetail {
            animeId: root.selectedAnimeId
            onOpenAnime: function(id) { root.openAnime(id) }
            onBackRequested: root.navigate("home")
        }
    }
}
