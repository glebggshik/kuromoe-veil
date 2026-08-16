.pragma library

// Общая логика источников/чипов для classic (DetailView.qml) и retro
// (RetroDetail.qml). Обе темы только ВЫЗЫВАЮТ эти функции — условия живут
// в одном месте, чтобы не расходились (вспышка «не найден», пропадающие
// чипы Torrent/Smash, разный double-click и т.п.).

// id переводов: "cvh_…", "kodik_…", "animetka_…", "hentasis_…", "anistar_…",
// "anilibria". Возвращает тип источника или "".
function sourceOf(id) {
    if (!id)
        return ""
    if (id.indexOf("cvh_") === 0)
        return "cvh"
    if (id.indexOf("kodik_") === 0)
        return "kodik"
    if (id.indexOf("animetka_") === 0)
        return "animetka"
    if (id.indexOf("hentasis_") === 0)
        return "hentasis"
    if (id.indexOf("anistar_") === 0)
        return "anistar"
    if (id === "anilibria")
        return "anilibria"
    return ""
}

// Чип Torrent: на обычном (не-хентай) тайтле виден ВСЕГДА.
//  - идёт поиск → label "TORRENT …" (см. torrentChipLabel)
//  - пусто / нет магнита → серый (disabled), НЕ прятать
//  - есть magnet/раздачи → активен
// Хентай не трогаем: там чип показываем как раньше (пока ищет/есть раздачи).
function torrentChipVisible(hentai, torrentsLoading, hasMagnet, torrentsLoaded, torrentCount) {
    if (hentai)
        return torrentsLoading || hasMagnet || (torrentsLoaded && torrentCount > 0)
    return true
}

function torrentChipEnabled(hasMagnet, torrentsLoading, torrentsLoaded, torrentCount) {
    return hasMagnet || (torrentsLoaded && !torrentsLoading && torrentCount > 0)
}

function torrentChipLabel(torrentsLoading) {
    return torrentsLoading ? "TORRENT …" : "TORRENT"
}

// Smash: виден если есть CVH или Kodik озвучка. Нет магнита → disabled
// (подсказка «нет раздачи» выводится отдельно), НЕ прятать.
function smashChipVisible(hentai, cvhAvailable, kodikAvailable, torrentChipVisible) {
    if (hentai)
        return false
    return (cvhAvailable || kodikAvailable) && torrentChipVisible
}

function smashChipEnabled(cvhAvailable, kodikAvailable, torrentEnabled) {
    return (cvhAvailable || kodikAvailable) && torrentEnabled
}

// Состояние источника из bridge.sourceStatus (source -> {state, message}).
// state: "loading" | "ok" | "empty" | "error"; null — статуса ещё нет.
function sourceStatus(key, statusMap) {
    const st = statusMap ? statusMap[key] : null
    return st ? st : null
}

// Красный текст источника ТОЛЬКО при error/empty. Пока loading или статуса
// нет — пустая строка (ничего не показывать; «ищем…» — на усмотрение темы
// через labels.loading, но по умолчанию ничего).
// labels: { empty: "…", errorPrefix: "Kodik: ошибка — " }
function sourceStatusText(key, statusMap, labels) {
    const st = sourceStatus(key, statusMap)
    if (!st)
        return ""
    if (st.state === "loading")
        return labels && labels.loading ? labels.loading : ""
    if (st.state === "error")
        return (labels && labels.errorPrefix ? labels.errorPrefix : "") + (st.message || "источник недоступен")
    if (st.state === "empty")
        return labels && labels.empty ? labels.empty : ""
    return "" // ok
}

// Нужно ли показывать «красный» текст источника (error/empty).
function sourceStatusShowError(key, statusMap) {
    const st = sourceStatus(key, statusMap)
    return !!st && (st.state === "error" || st.state === "empty")
}
