import QtQuick

// CRT-сканлайны (.crt-scanlines из globals.css) — повторяющиеся горизонтальные
// линии шагом 3px. Canvas дешевле сотен Rectangle на большой площади (hero).
Canvas {
    id: canvas
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = "rgba(56, 248, 146, 0.05)"
        var y = 0
        while (y < height) {
            ctx.fillRect(0, y, width, 1)
            y += 3
        }
    }
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
}
