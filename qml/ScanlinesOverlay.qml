import QtQuick

// CRT-сканлайны (.crt-scanlines из globals.css) — повторяющиеся горизонтальные
// линии шагом 3px. Canvas дешевле сотен Rectangle на большой площади (hero).
Canvas {
    id: canvas
    onPaint: {
        var ctx = getContext("2d")
        // reset() сбрасывает только состояние контекста (fillStyle и т.п.),
        // но НЕ очищает пиксели — без явного clearRect() каждый повторный
        // repaint (при любом изменении размеров/layout, а их за время жизни
        // экрана десятки) рисует ещё один полупрозрачный слой линий поверх
        // старого. Слои копятся — картинка постепенно темнеет/зеленеет и
        // выглядит как непрерывное мигание.
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
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
