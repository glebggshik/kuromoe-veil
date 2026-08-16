pragma Singleton
import QtQuick

// Копия палитры из src/core/Theme.h — держи в синхроне вручную, если
// поменяешь цвета в реальном приложении и хочешь, чтобы песочница совпадала.
QtObject {
    readonly property string accent: "#a855f7"
    readonly property string accentHover: "#9333ea"
    readonly property string accentLight: "#c084fc"
    readonly property string bgApp: "#0a0a0f"
    readonly property string bgSidebar: "#111116"
    readonly property string bgCard: "#16161c"
    readonly property string bgCardHover: "#1f1f28"
    readonly property string bgInput: "#16161c"
    readonly property string bgPill: "#1c1c24"
    readonly property string textPrimary: "#f1f1f5"
    readonly property string textSecondary: "#a1a1aa"
    readonly property string textMuted: "#71717a"
    readonly property string good: "#22c55e"
    readonly property string warn: "#eab308"
    readonly property string bad: "#ef4444"
    readonly property int corner: 16
    readonly property int cornerSmall: 10
    readonly property int cornerPill: 22
}
