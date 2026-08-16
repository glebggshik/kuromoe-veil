.pragma library

// Портировано из retro-anime-app/lib/anime-data.ts — те же тайтлы/жанры/
// синопсисы, картинки скопированы в assets/retro/.

function hero() {
    return {
        title: "NEON RONIN",
        tagline: "The last blade in a city of static.",
        synopsis: "In the drowned megacity of Neo-Kanda, a masterless swordsman hunts the corporate ghosts that erased his past. Every kill brings him one byte closer to the truth.",
        banner: "assets/retro/hero.png",
        year: 2024,
        rating: 9.2,
        genres: ["CYBERPUNK", "ACTION", "SEINEN"]
    }
}

function items() {
    return [
        { id: "cherry-blade", title: "CHERRY BLADE", cover: "assets/retro/cover-1.png",
          year: 2023, rating: 8.7, episodes: 24, genres: ["SAMURAI", "DRAMA"], status: "ONGOING",
          synopsis: "A wandering ronin under eternal blossoms seeks the swordsman who took everything from her." },
        { id: "null-pointer", title: "NULL POINTER", cover: "assets/retro/cover-2.png",
          year: 2024, rating: 9.0, episodes: 12, genres: ["CYBERPUNK", "THRILLER"], status: "ONGOING",
          synopsis: "A rogue netrunner dives into a corrupted mainframe and finds a consciousness that should not exist." },
        { id: "iron-vanguard", title: "IRON VANGUARD", cover: "assets/retro/cover-3.png",
          year: 2022, rating: 8.4, episodes: 50, genres: ["MECHA", "WAR"], status: "ONGOING",
          synopsis: "Humanity's last mecha corps holds the line against an unknowable force from beyond the fold." },
        { id: "emerald-oath", title: "EMERALD OATH", cover: "assets/retro/cover-4.png",
          year: 2023, rating: 8.1, episodes: 26, genres: ["FANTASY", "MAGIC"], status: "ONGOING",
          synopsis: "A young mage bound by a forbidden pact walks the haunted greenwood to break her curse." },
        { id: "star-bounty", title: "STAR BOUNTY", cover: "assets/retro/cover-5.png",
          year: 2021, rating: 8.9, episodes: 38, genres: ["SPACE", "ADVENTURE"], status: "COMPLETED",
          synopsis: "A washed-up bounty hunter takes one final contract that spans the entire ringed frontier." },
        { id: "rain-protocol", title: "RAIN PROTOCOL", cover: "assets/retro/cover-6.png",
          year: 2024, rating: 8.6, episodes: 11, genres: ["NOIR", "MYSTERY"], status: "ONGOING",
          synopsis: "A detective with a debugged memory chases a serial signal through the neon underbelly." }
    ]
}

function getAnime(id) {
    var all = items()
    for (var i = 0; i < all.length; ++i)
        if (all[i].id === id)
            return all[i]
    return all[0]
}

// Портировано из LIBRARY в anime-data.ts
function library() {
    return [
        { animeId: "cherry-blade", status: "WATCHING", watched: 14 },
        { animeId: "null-pointer", status: "WATCHING", watched: 8 },
        { animeId: "rain-protocol", status: "WATCHING", watched: 3 },
        { animeId: "iron-vanguard", status: "ON HOLD", watched: 22 },
        { animeId: "emerald-oath", status: "ON HOLD", watched: 5 },
        { animeId: "star-bounty", status: "COMPLETED", watched: 38 }
    ]
}

function sources() {
    return ["ZORO-MIRROR", "GOGO-CDN", "ANIMIX-04", "BACKUP-SRV"]
}

function voiceovers() {
    return ["SUB [JP]", "DUB [EN]", "DUB [ES]", "SUB [KR]"]
}
