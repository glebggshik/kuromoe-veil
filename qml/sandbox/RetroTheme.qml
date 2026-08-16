pragma Singleton
import QtQuick

// Палитра из retro-anime-app/app/globals.css (фосфорный терминал: зелёный
// primary + янтарный accent, острые углы, CRT-сканлайны). Портировано из
// Tailwind CSS custom properties 1:1.
QtObject {
    readonly property string background: "#060807"
    readonly property string foreground: "#cfe8d2"

    readonly property string card: "#0c0f0d"
    readonly property string cardForeground: "#cfe8d2"

    readonly property string primary: "#38f892"        // phosphor green
    readonly property string primaryForeground: "#05130a"

    readonly property string muted: "#10140f"
    readonly property string mutedForeground: "#6f8172"

    readonly property string accent: "#ffb32b"          // amber
    readonly property string accentForeground: "#1a1204"

    readonly property string destructive: "#ff5a4d"

    readonly property string border: "#1e2a20"
    readonly property string input: "#16201a"

    readonly property string sidebar: "#080a09"
    readonly property string sidebarForeground: "#8fa392"
    readonly property string sidebarAccent: "#10160f"

    // Sharp edges everywhere — оригинал форсирует radius:0 везде.
    readonly property int radius: 0

    readonly property string fontFamily: "Consolas"    // ближайший моно-шрифт из коробки Windows
}
