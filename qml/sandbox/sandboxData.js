.pragma library

// Фейковые данные для песочницы дизайна — те же поля, что реально приходят
// от CatalogBridge (title/poster/kind/episodes/score/year/genreTags), чтобы
// скопированные компоненты работали без изменений в логике.
function items() {
    return [
        { title: "Ван-Пис", kind: "tv", episodes: 1168, score: 8.73, year: 1999,
          genreTags: [{ id: 1, name: "Экшен" }, { id: 2, name: "Приключения" }] },
        { title: "Детектив Конан", kind: "tv", episodes: 1205, score: 8.18, year: 1996,
          genreTags: [{ id: 3, name: "Тайна" }] },
        { title: "Re:Zero. Жизнь с нуля в альтернативном мире 4", kind: "tv", episodes: 19, score: 9.19, year: 2026,
          genreTags: [{ id: 4, name: "Драма" }, { id: 5, name: "Фэнтези" }] },
        { title: "О моём перерождении в слизь 4", kind: "ona", episodes: 12, score: 8.09, year: 2026,
          genreTags: [] },
        { title: "Тикава", kind: "tv", episodes: 253, score: 8.60, year: 2022,
          genreTags: [{ id: 6, name: "Повседневность" }] },
        { title: "Мир танцует", kind: "tv", episodes: 1, score: 7.35, year: 2026,
          genreTags: [{ id: 7, name: "Драма" }] },
        { title: "Ёж", kind: "movie", episodes: 1, score: 0, year: 2010,
          genreTags: [] },
        { title: "Тайна безымянной деревни в горах", kind: "tv", episodes: 24, score: 7.90, year: 2018,
          genreTags: [{ id: 8, name: "Ужасы" }, { id: 9, name: "Мистика" }, { id: 10, name: "Триллер" }] },
        { title: "Кот и дракон", kind: "ona", episodes: 1, score: 7.54, year: 2026,
          genreTags: [{ id: 11, name: "Фэнтези" }] },
        { title: "Безупречный мир", kind: "tv", episodes: 273, score: 8.31, year: 2021,
          genreTags: [{ id: 1, name: "Экшен" }] },
        { title: "Sword Art Online: Progressive", kind: "movie", episodes: 1, score: 7.28, year: 2021,
          genreTags: [{ id: 5, name: "Фэнтези" }] },
        { title: "?", kind: "", episodes: 0, score: 0, year: 0, genreTags: [] }
    ]
}
