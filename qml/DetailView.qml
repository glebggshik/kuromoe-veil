import QtQuick
import QtQuick.Controls
import AnimeClient 1.0
import "components"

// Экран тайтла — порт qml/DetailView.qml (Python-версия), метаданные/статус/
// озвучки/торренты/связанное — через C++ DetailBridge (1:1 интерфейс со
// старым мостом). Само воспроизведение — НЕ через бридж (там раньше mpv был
// отдельным OS-окном), а через PlaybackController+MpvPlayer, встроенные прямо
// в эту сцену; DetailBridge только резолвит URL/magnet и просит
// PlaybackController сыграть их (см. DetailBridge::play в C++).
Item {
    id: root
    property var item: ({})
    signal backRequested()
    signal openDetail(var item)
    signal genreClicked(int genreId)
    signal yearClicked(int year)

    readonly property int contentWidth: Math.min(width - 64, 760)

    property var translations: []
    property bool anilibriaAvailable: false
    property int episode: 1
    // sourceType: "" | "cvh" | "kodik" | "animetka" | "hentasis" | "anistar" | "anilibria" | "torrent" | "smash"
    property string sourceType: ""
    property string translationId: ""
    // Качество для Animetka: "" / "best" = лучшее; иначе "1080","720",…
    property string streamQuality: ""
    onTranslationIdChanged: {
        if (root.episode > root.currentMaxEpisode) root.episode = root.currentMaxEpisode
        playback.setTotalEpisodes(root.currentMaxEpisode)
        root.syncQualityForTranslation()
    }
    onSourceTypeChanged: {
        if (root.episode > root.currentMaxEpisode) root.episode = root.currentMaxEpisode
        playback.setTotalEpisodes(root.currentMaxEpisode)
        root.syncQualityForTranslation()
    }

    function isCvhTranslationId(id) {
        return id !== "" && id.indexOf("cvh_") === 0
    }
    function isKodikTranslationId(id) {
        return id !== "" && id.indexOf("kodik_") === 0
    }
    function isAnimetkaTranslationId(id) {
        return id !== "" && id.indexOf("animetka_") === 0
    }
    function isHentasisTranslationId(id) {
        return id !== "" && id.indexOf("hentasis_") === 0
    }
    function isAnistarTranslationId(id) {
        return id !== "" && id.indexOf("anistar_") === 0
    }
    function syncQualityForTranslation() {
        if (root.sourceType !== "animetka") {
            root.streamQuality = ""
            return
        }
        var qs = root.currentAnimetkaQualities
        if (qs.length === 0) {
            root.streamQuality = ""
            return
        }
        if (root.streamQuality !== "" && qs.indexOf(root.streamQuality) >= 0)
            return
        // лучшее сверху (сортируем числами)
        var best = qs[0]
        for (var i = 0; i < qs.length; i++) {
            if (parseInt(qs[i]) > parseInt(best)) best = qs[i]
        }
        root.streamQuality = best
    }
    readonly property var currentAnimetkaQualities: {
        if (!root.isAnimetkaTranslationId(root.translationId)) return []
        for (var i = 0; i < root.animetkaTranslations.length; i++) {
            if (root.animetkaTranslations[i].id === root.translationId) {
                var q = root.animetkaTranslations[i].qualities || []
                return q.slice().sort(function(a, b) { return parseInt(b) - parseInt(a) })
            }
        }
        return []
    }
    function isHentaiItem() {
        var tags = root.item.genreTags || []
        for (var i = 0; i < tags.length; i++) {
            if (tags[i].id === 12) return true
        }
        return false
    }
    readonly property bool hentaiItem: root.isHentaiItem()

    // translations — общий список от DetailBridge (CVH + Kodik вперемешку,
    // отличаются префиксом id), разделяем для UI/выбора источника.
    readonly property var cvhTranslations: root.translations.filter(function(t) { return root.isCvhTranslationId(t.id) })
    readonly property var kodikTranslations: root.translations.filter(function(t) { return root.isKodikTranslationId(t.id) })
    readonly property var animetkaTranslations: root.translations.filter(function(t) { return root.isAnimetkaTranslationId(t.id) })
    readonly property var hentasisTranslations: root.translations.filter(function(t) { return root.isHentasisTranslationId(t.id) })
    readonly property var anistarTranslations: root.translations.filter(function(t) { return root.isAnistarTranslationId(t.id) })
    readonly property bool cvhAvailable: root.translationsLoaded && root.cvhTranslations.length > 0
    readonly property bool kodikAvailable: root.translationsLoaded && root.kodikTranslations.length > 0
    readonly property bool animetkaAvailable: root.translationsLoaded && root.animetkaTranslations.length > 0
    readonly property bool hentasisAvailable: root.translationsLoaded && root.hentasisTranslations.length > 0
    readonly property bool anistarAvailable: root.translationsLoaded && root.anistarTranslations.length > 0
    readonly property bool anilibriaSourceAvailable: root.anilibriaChecked && root.anilibriaAvailable
    // Пока идёт поиск — кнопка видна. После завершения скрываем, если раздач нет
    // (и нет сохранённого magnet).
    readonly property bool torrentSourceVisible: root.hentaiItem
        || root.torrentsLoading
        || root.lastTorrentMagnet !== ""
        || (root.torrentsLoaded && root.torrents.length > 0)
    readonly property bool torrentSourceEnabled: root.lastTorrentMagnet !== ""
        || (root.torrentsLoaded && !root.torrentsLoading && root.torrents.length > 0)
    readonly property bool torrentSourceAvailable: root.torrentSourceEnabled

    function effectiveTranslationId() {
        if (root.sourceType === "anilibria") return "anilibria"
        if (root.sourceType === "torrent") return "torrent"
        if (root.sourceType === "cvh" || root.sourceType === "kodik"
            || root.sourceType === "hentasis" || root.sourceType === "anistar") return root.translationId
        if (root.sourceType === "animetka") {
            if (root.streamQuality && root.streamQuality !== "" && root.streamQuality !== "best") {
                // animetka_610 → animetka_610_q1080
                var base = root.translationId
                var qpos = base.indexOf("_q")
                if (qpos > 0) base = base.substring(0, qpos)
                return base + "_q" + root.streamQuality
            }
            return root.translationId
        }
        if (root.sourceType === "smash") return root.translationId
        return ""
    }

    // Для Smash: автовыбор лучшей голосовой озвучки (CVH vs Kodik — у кого больше)
    function smashBestVoiceId() {
        var src = root.cvhTranslations.length >= root.kodikTranslations.length
            ? root.cvhTranslations : root.kodikTranslations
        return src.length > 0 ? src[0].id : ""
    }

    function selectSource(type) {
        if (type === "cvh") {
            if (!root.cvhAvailable) return
            root.sourceType = "cvh"
            if (!root.isCvhTranslationId(root.translationId) && root.cvhTranslations.length > 0)
                root.translationId = root.cvhTranslations[0].id
        } else if (type === "kodik") {
            if (!root.kodikAvailable) return
            root.sourceType = "kodik"
            if (!root.isKodikTranslationId(root.translationId) && root.kodikTranslations.length > 0)
                root.translationId = root.kodikTranslations[0].id
        } else if (type === "animetka") {
            if (!root.animetkaAvailable) return
            root.sourceType = "animetka"
            if (!root.isAnimetkaTranslationId(root.translationId) && root.animetkaTranslations.length > 0)
                root.translationId = root.animetkaTranslations[0].id
            root.syncQualityForTranslation()
        } else if (type === "hentasis") {
            if (!root.hentasisAvailable) return
            root.sourceType = "hentasis"
            if (!root.isHentasisTranslationId(root.translationId) && root.hentasisTranslations.length > 0)
                root.translationId = root.hentasisTranslations[0].id
        } else if (type === "anistar") {
            if (!root.anistarAvailable) return
            root.sourceType = "anistar"
            if (!root.isAnistarTranslationId(root.translationId) && root.anistarTranslations.length > 0)
                root.translationId = root.anistarTranslations[0].id
        } else if (type === "anilibria") {
            if (!root.anilibriaSourceAvailable) return
            root.sourceType = "anilibria"
            root.translationId = "anilibria"
        } else if (type === "torrent") {
            if (!root.torrentSourceEnabled) return
            root.sourceType = "torrent"
            root.translationId = "torrent"
        } else if (type === "smash") {
            root.sourceType = "smash"
            // Авто-выбор озвучки — у кого больше, тот приоритет
            var bestId = root.smashBestVoiceId()
            if (bestId !== "") root.translationId = bestId
            // Торрент — если есть, берём первый
            if (root.lastTorrentMagnet === "" && root.torrents.length > 0)
                bridge.selectTorrent(root.torrents[0].magnet)
        }
    }

    function applySavedSource(translationIdArg) {
        if (root.isCvhTranslationId(translationIdArg)) {
            root.sourceType = "cvh"
            root.translationId = translationIdArg
        } else if (root.isKodikTranslationId(translationIdArg)) {
            root.sourceType = "kodik"
            root.translationId = translationIdArg
        } else if (root.isAnimetkaTranslationId(translationIdArg)) {
            root.sourceType = "animetka"
            // убрать _qNNN из сохранённого id
            var base = translationIdArg
            var qpos = base.indexOf("_q")
            if (qpos > 0) {
                root.streamQuality = base.substring(qpos + 2)
                base = base.substring(0, qpos)
            }
            root.translationId = base
        } else if (root.isHentasisTranslationId(translationIdArg)) {
            root.sourceType = "hentasis"
            root.translationId = translationIdArg
        } else if (root.isAnistarTranslationId(translationIdArg)) {
            root.sourceType = "anistar"
            root.translationId = translationIdArg
        } else if (translationIdArg === "anilibria") {
            root.sourceType = "anilibria"
            root.translationId = "anilibria"
        } else if (translationIdArg === "torrent") {
            root.sourceType = "torrent"
            root.translationId = "torrent"
        }
    }

    readonly property bool canPlay: {
        if (root.sourceType === "cvh")
            return root.cvhAvailable && root.isCvhTranslationId(root.translationId)
        if (root.sourceType === "kodik")
            return root.kodikAvailable && root.isKodikTranslationId(root.translationId)
        if (root.sourceType === "animetka")
            return root.animetkaAvailable && root.isAnimetkaTranslationId(root.translationId)
        if (root.sourceType === "hentasis")
            return root.hentasisAvailable && root.isHentasisTranslationId(root.translationId)
        if (root.sourceType === "anistar")
            return root.anistarAvailable && root.isAnistarTranslationId(root.translationId)
        if (root.sourceType === "anilibria")
            return root.anilibriaSourceAvailable
        if (root.sourceType === "torrent")
            return root.torrentSourceAvailable && root.lastTorrentMagnet !== ""
        if (root.sourceType === "smash")
            return (root.isCvhTranslationId(root.translationId) || root.isKodikTranslationId(root.translationId))
                || root.lastTorrentMagnet !== ""
        return false
    }
    property var relatedModel: []
    property int anilibriaEpisodes: 0
    property bool translationsLoaded: false
    property bool anilibriaChecked: false
    property var torrents: []
    property bool torrentsLoading: false
    property bool torrentsLoaded: false

    readonly property string releaseDateText: {
        if ((root.item.year || 0) === 0) return "неизвестно"
        var months = ["", "января", "февраля", "марта", "апреля", "мая", "июня",
                      "июля", "августа", "сентября", "октября", "ноября", "декабря"]
        if (root.item.airedDay > 0 && root.item.airedMonth > 0) {
            return root.item.airedDay + " " + months[root.item.airedMonth] + " " + root.item.year
        }
        return String(root.item.year)
    }

    // у выбранной озвучки серий часто меньше, чем у тайтла на Шикимори
    // (переводчики не успевают за выходом) — поле "Серия" должно
    // ограничиваться именно этим числом, а не общим количеством
    // item.episodes — общее число серий, 0 пока тайтл онгоинг (Шикимори ещё
    // не знает финал) — тогда ограничиваемся episodesAired (сколько реально
    // вышло), иначе двигаться по сериям было некуда (заглушка 9999).
    readonly property int knownEpisodeCount: root.item.episodes || root.item.episodesAired || 9999
    readonly property int currentMaxEpisode: {
        if (root.sourceType === "anilibria") {
            return root.anilibriaEpisodes > 0 ? root.anilibriaEpisodes : root.knownEpisodeCount
        }
        if (root.sourceType === "cvh" || root.sourceType === "kodik"
            || root.sourceType === "animetka"
            || root.sourceType === "hentasis" || root.sourceType === "anistar") {
            for (var i = 0; i < root.translations.length; i++) {
                if (root.translations[i].id === root.translationId && root.translations[i].episodes > 0) {
                    return root.translations[i].episodes
                }
            }
        }
        return root.knownEpisodeCount
    }

    // на чём пользователь остановился прошлый раз — приходит из HistoryManager
    // ещё до того, как переводы/AniLibria подгрузятся, поэтому применяется
    // и сразу (episode), и повторно при их подгрузке (translationId)
    property int savedEpisode: 1
    property string savedTranslationId: ""
    property string lastTorrentMagnet: ""
    property bool cinemaMode: false
    property bool showPlayerPanel: false

    PlaybackController {
        id: playback
        onResumeAvailable: (ep, positionSeconds, translationIdArg) => {
            resumePrompt.episode = ep
            resumePrompt.positionSeconds = positionSeconds
            resumePrompt.visible = true
            root.applySavedSource(translationIdArg)
        }
        onErrorOccurred: (msg) => statusText.text = msg
        onTorrentFilesReady: function(files) {
            torrentFilePicker.files = files
            torrentFilePicker.visible = true
        }
        onEpisodeChanged: {
            if (playback.currentEpisode > 0)
                root.episode = playback.currentEpisode
        }
        onNextEpisodeNeeded: (ep) => {
            root.episode = ep
            var tid = root.effectiveTranslationId()
            if (tid !== "")
                bridge.play(ep, tid)
        }
    }

    DetailBridge {
        id: bridge
        Component.onCompleted: attachPlaybackController(playback)
        onDetailsReady: function(d) {
            root.item = d
        }
        onProgressReady: function(ep, translationIdArg, torrentMagnet) {
            root.savedEpisode = ep
            root.savedTranslationId = translationIdArg
            root.lastTorrentMagnet = torrentMagnet
            root.episode = ep
            root.applySavedSource(translationIdArg)
        }
        onTranslationsReady: function(list) {
            root.translations = list
            root.translationsLoaded = true
            function sourceOf(id) {
                if (root.isCvhTranslationId(id)) return "cvh"
                if (root.isKodikTranslationId(id)) return "kodik"
                if (root.isAnimetkaTranslationId(id)) return "animetka"
                if (root.isHentasisTranslationId(id)) return "hentasis"
                if (root.isAnistarTranslationId(id)) return "anistar"
                return ""
            }
            function baseAnimetkaId(id) {
                if (!root.isAnimetkaTranslationId(id)) return id
                var qpos = id.indexOf("_q")
                return qpos > 0 ? id.substring(0, qpos) : id
            }
            if (root.isCvhTranslationId(root.savedTranslationId) || root.isKodikTranslationId(root.savedTranslationId)
                || root.isAnimetkaTranslationId(root.savedTranslationId)
                || root.isHentasisTranslationId(root.savedTranslationId) || root.isAnistarTranslationId(root.savedTranslationId)) {
                var wantId = baseAnimetkaId(root.savedTranslationId)
                var saved = list.find(function(t) { return t.id === wantId || t.id === root.savedTranslationId })
                if (saved) {
                    root.sourceType = sourceOf(saved.id)
                    root.translationId = saved.id
                    if (root.sourceType === "animetka") {
                        var qpos2 = root.savedTranslationId.indexOf("_q")
                        if (qpos2 > 0) root.streamQuality = root.savedTranslationId.substring(qpos2 + 2)
                        root.syncQualityForTranslation()
                    }
                }
            } else if (root.sourceType === "") {
                if (root.hentaiItem) {
                    if (root.hentasisTranslations.length > 0) {
                        root.sourceType = "hentasis"
                        root.translationId = root.hentasisTranslations[0].id
                    } else if (root.anistarTranslations.length > 0) {
                        root.sourceType = "anistar"
                        root.translationId = root.anistarTranslations[0].id
                    } else if (root.kodikTranslations.length > 0) {
                        root.sourceType = "kodik"
                        root.translationId = root.kodikTranslations[0].id
                    }
                } else if (list.length > 0) {
                    root.sourceType = sourceOf(list[0].id)
                    root.translationId = list[0].id
                }
            }
            playback.setTotalEpisodes(root.currentMaxEpisode)
        }
        onAnilibriaReady: function(ok) {
            root.anilibriaAvailable = ok
            root.anilibriaChecked = true
            if (root.savedTranslationId === "anilibria" && ok)
                root.applySavedSource("anilibria")
        }
        onAnilibriaEpisodesReady: function(count) {
            root.anilibriaEpisodes = count
            if (root.sourceType === "anilibria")
                playback.setTotalEpisodes(root.currentMaxEpisode)
        }
        onRelatedReady: function(items) { root.relatedModel = items }
        onError: function(msg) { statusText.text = msg }
        onTorrentsReady: function(list) {
            root.torrents = list
            root.torrentsLoaded = true
            root.torrentsLoading = false
            if (root.savedTranslationId === "torrent" && root.lastTorrentMagnet !== "")
                root.applySavedSource("torrent")
            else if (root.hentaiItem && list.length > 0 && root.sourceType === ""
                && !root.hentasisAvailable && !root.anistarAvailable && !root.kodikAvailable) {
                bridge.selectTorrent(list[0].magnet)
                root.lastTorrentMagnet = list[0].magnet
                root.applySavedSource("torrent")
            } else if (list.length === 0 && root.lastTorrentMagnet === "" && root.sourceType === "torrent") {
                root.sourceType = ""
                root.translationId = ""
            }
        }
        onTorrentsLoading: function(loading) { root.torrentsLoading = loading }
    }

    property string _loadedId: ""

    function resetForNewItem() {
        root.translations = []
        root.translationsLoaded = false
        root.anilibriaAvailable = false
        root.anilibriaChecked = false
        root.anilibriaEpisodes = 0
        root.sourceType = ""
        root.translationId = ""
        root.relatedModel = []
        root.torrents = []
        root.torrentsLoaded = false
        root.torrentsLoading = false
        root.episode = 1
        root.savedEpisode = 1
        root.savedTranslationId = ""
        root.lastTorrentMagnet = ""
        statusText.text = ""
    }

    function loadCurrentItem() {
        if (!root.item || !root.item.id || root.item.id === root._loadedId)
            return
        root._loadedId = root.item.id
        resetForNewItem()
        bridge.load(root.item)
    }

    onItemChanged: loadCurrentItem()
    Component.onCompleted: loadCurrentItem()
    // Полноэкранный плеер в том же окне (отдельный Window ломает OpenGL-контекст mpv)
    Rectangle {
        id: cinemaLayer
        anchors.fill: parent
        visible: root.cinemaMode && root.showPlayerPanel
        z: 500
        color: "#000000"
        clip: true

        focus: visible
        Keys.onEscapePressed: function(event) {
            root.cinemaMode = false
            event.accepted = true
        }
    }

    Flickable {
        id: detailFlick
        anchors.fill: parent
        visible: !root.cinemaMode
        contentWidth: width
        contentHeight: column.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.contentWidth
            spacing: 16
            topPadding: 20

            Row {
                spacing: 8
                Button {
                    text: "← Назад"
                    implicitHeight: 34
                    background: Rectangle { radius: Theme.cornerPill; color: Theme.bgPill }
                    contentItem: Text { text: "← Назад"; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: root.backRequested()
                }
            }

            Row {
                width: parent.width
                spacing: 20

                Rectangle {
                    width: 180; height: 260
                    radius: Theme.corner
                    color: Theme.bgCard
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: {
                            var hd = root.item.posterHd || ""
                            var p = root.item.poster || ""
                            function ok(u) { return u.length > 0 && !u.endsWith(".webp") }
                            if (ok(hd)) return hd
                            if (ok(p)) return p
                            return hd || p
                        }
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                    }
                }

                Column {
                    width: parent.width - 200
                    spacing: 8

                    Text {
                        width: parent.width
                        text: root.item.title || ""
                        color: Theme.textPrimary
                        font.pixelSize: 22
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: [root.item.kind, root.item.status, root.item.episodes ? root.item.episodes + " эп." : ""].filter(function(s){return s}).join("  ·  ")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Row {
                        spacing: 4
                        visible: (root.item.score || 0) > 0
                        Text { text: "★"; color: Theme.warn; font.pixelSize: 14 }
                        Text { text: root.item.score || ""; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                    }
                    Text {
                        width: parent.width
                        text: root.item.description || ""
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        maximumLineCount: 6
                        elide: Text.ElideRight
                    }

                    // все жанры + год, кликабельны — ведут на список аниме
                    // с этим же фильтром (см. genreClicked/yearClicked)
                    Flow {
                        width: parent.width
                        topPadding: 4
                        spacing: 6

                        Repeater {
                            model: root.item.genreTags || []
                            delegate: PillButton {
                                text: modelData.name
                                onClicked: root.genreClicked(modelData.id)
                            }
                        }

                        PillButton {
                            visible: (root.item.year || 0) > 0
                            text: String(root.item.year || "")
                            onClicked: root.yearClicked(root.item.year)
                        }
                    }
                }
            }

            // === Статус ===
            Column {
                width: parent.width
                spacing: 8
                Text { text: "Статус"; color: Theme.textSecondary; font.pixelSize: 12 }
                Flow {
                    width: parent.width
                    spacing: 6
                    StatusPill { label: "Смотрю"; value: "watching"; current: bridge.currentStatus; onPicked: function(s) { bridge.setStatus(s) } }
                    StatusPill { label: "Просмотрено"; value: "watched"; current: bridge.currentStatus; onPicked: function(s) { bridge.setStatus(s) } }
                    StatusPill { label: "Брошено"; value: "dropped"; current: bridge.currentStatus; onPicked: function(s) { bridge.setStatus(s) } }
                    StatusPill { label: "В планах"; value: "planned"; current: bridge.currentStatus; onPicked: function(s) { bridge.setStatus(s) } }
                    StatusPill { label: "Отложено"; value: "postponed"; current: bridge.currentStatus; onPicked: function(s) { bridge.setStatus(s) } }
                }
            }

            // === Источник: 18+ — Hentasis/AniStar/Kodik/Torrent; иначе CVH/Kodik/AniLibria ===
            Column {
                width: parent.width
                spacing: 8
                visible: !!(root.item && root.item.id)
                Text { text: "Источник"; color: Theme.textSecondary; font.pixelSize: 12 }
                Flow {
                    width: parent.width
                    spacing: 6
                    PillButton {
                        text: "Hentasis"
                        visible: root.hentaiItem
                        active: root.sourceType === "hentasis"
                        enabled: root.hentasisAvailable
                        onClicked: root.selectSource("hentasis")
                    }
                    PillButton {
                        text: "AniStar"
                        visible: root.hentaiItem
                        active: root.sourceType === "anistar"
                        enabled: root.anistarAvailable
                        onClicked: root.selectSource("anistar")
                    }
                    PillButton {
                        text: "CVH"
                        visible: !root.hentaiItem
                        active: root.sourceType === "cvh"
                        enabled: root.cvhAvailable
                        onClicked: root.selectSource("cvh")
                    }
                    PillButton {
                        text: "Kodik"
                        active: root.sourceType === "kodik"
                        enabled: root.kodikAvailable
                        onClicked: root.selectSource("kodik")
                    }
                    PillButton {
                        text: "Animetka"
                        active: root.sourceType === "animetka"
                        enabled: root.animetkaAvailable
                        onClicked: root.selectSource("animetka")
                    }
                    PillButton {
                        text: "AniLibria"
                        visible: !root.hentaiItem
                        active: root.sourceType === "anilibria"
                        enabled: root.anilibriaSourceAvailable
                        onClicked: root.selectSource("anilibria")
                    }
                    PillButton {
                        text: root.torrentsLoading ? "Torrent …" : "Torrent"
                        visible: root.torrentSourceVisible
                        active: root.sourceType === "torrent"
                        enabled: root.torrentSourceEnabled
                        onClicked: root.selectSource("torrent")
                    }
                    PillButton {
                        text: "Smash 💥"
                        visible: !root.hentaiItem && root.torrentSourceVisible && (root.cvhAvailable || root.kodikAvailable)
                        active: root.sourceType === "smash"
                        enabled: (root.cvhAvailable || root.kodikAvailable) && root.torrentSourceEnabled
                        onClicked: root.selectSource("smash")
                    }
                }
                Text {
                    visible: root.hentaiItem && !root.translationsLoaded
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: "Ищем потоки на Hentasis / AniStar / Kodik…"
                }
                Text {
                    visible: root.hentaiItem && root.translationsLoaded
                        && !root.hentasisAvailable && !root.anistarAvailable && !root.kodikAvailable
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: root.torrentsLoading
                        ? "Стримы не найдены — ищем торренты (Sukebei / AniStar)…"
                        : (root.torrents.length > 0
                            ? "Прямой стрим недоступен — выбери торрент ниже (TorrServer)."
                            : "Ничего не найдено. Проверь прокси (AniStar/Kodik) и TorrServer.")
                }
                Text {
                    visible: root.sourceType === "anilibria" && root.anilibriaChecked
                    text: root.anilibriaAvailable
                        ? (root.anilibriaEpisodes > 0
                            ? "В каталоге · " + root.anilibriaEpisodes + " серий"
                            : "В каталоге AniLibria")
                        : "Нет в каталоге AniLibria"
                    color: root.anilibriaAvailable ? Theme.textSecondary : Theme.bad
                    font.pixelSize: 11
                }
                Text {
                    visible: root.sourceType === "cvh" && root.translationsLoaded && !root.cvhAvailable
                    text: "CVH: озвучки не найдены на AnimeGO (для этого тайтла может не быть CVH)"
                    color: Theme.bad
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
                Text {
                    visible: root.sourceType === "kodik" && root.translationsLoaded && !root.kodikAvailable
                    text: "Kodik: озвучки не найдены (см. Настройки → прокси, либо временная блокировка)"
                    color: Theme.bad
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
                Text {
                    visible: root.sourceType === "animetka" && root.translationsLoaded && !root.animetkaAvailable
                    text: "Animetka: тайтл не найден или API недоступен"
                    color: Theme.bad
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
                Text {
                    visible: root.sourceType === "hentasis" && root.translationsLoaded && !root.hentasisAvailable
                    text: "Hentasis: не найдено (зеркала могут быть недоступны)"
                    color: Theme.bad
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
                Text {
                    visible: root.sourceType === "anistar" && root.translationsLoaded && !root.anistarAvailable
                    text: "AniStar: прямой стрим не найден (часто только торренты на сайте)"
                    color: Theme.bad
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
                // Список раздач — сразу под Torrent (как озвучки у CVH)
                Column {
                    width: parent.width
                    spacing: 10
                    visible: root.sourceType === "torrent" || root.sourceType === "smash"

                    Text {
                        width: parent.width
                        text: root.torrentsLoading
                            ? "Ищу торренты..."
                            : (root.torrents.length > 0
                                ? "Найдено: " + root.torrents.length
                                : (root.torrentsLoaded
                                    ? "Ничего не найдено — проверь JacRed/прокси в Настройках"
                                    : ""))
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        visible: text.length > 0
                    }

                    Flickable {
                        id: torrentsFlick
                        width: parent.width
                        height: Math.min(torrentsList.height, 280)
                        visible: root.torrents.length > 0
                        contentWidth: width
                        contentHeight: torrentsList.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        // Колесо мыши тут не листает список (для этого — свайп/
                        // драг), а прокручивает страницу целиком — иначе наведение
                        // на список торрентов "запирает" прокрутку вниз.
                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse
                            onWheel: function(event) {
                                const max = Math.max(0, detailFlick.contentHeight - detailFlick.height)
                                detailFlick.contentY = Math.max(0, Math.min(max, detailFlick.contentY - event.angleDelta.y))
                            }
                        }

                        Column {
                            id: torrentsList
                            width: torrentsFlick.width
                            spacing: 10

                            Repeater {
                                model: root.torrents
                                delegate: Rectangle {
                                    readonly property bool isSelected: (root.sourceType === "torrent" || root.sourceType === "smash")
                                        && modelData.magnet === root.lastTorrentMagnet
                                    width: torrentsList.width
                                    radius: Theme.cornerSmall
                                    color: isSelected ? Theme.bgCardHover : Theme.bgCard
                                    border.width: isSelected ? 2 : 1
                                    border.color: isSelected ? Theme.accent : Theme.bgPill
                                    height: torrentBody.height + 24

                                    Row {
                                        id: torrentBody
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 12
                                        spacing: 12

                                        Column {
                                            width: parent.width - selectedBadge.width - parent.spacing
                                            spacing: 6

                                            Text {
                                                width: parent.width
                                                text: modelData.title || "?"
                                                color: Theme.textPrimary
                                                font.pixelSize: 13
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 3
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                width: parent.width
                                                text: [modelData.tracker, modelData.size, modelData.seeders !== undefined ? "сидов: " + modelData.seeders : ""].filter(function(s){return s}).join("  ·  ")
                                                color: Theme.textSecondary
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Text {
                                            id: selectedBadge
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: isSelected
                                            text: "✓"
                                            color: Theme.accent
                                            font.pixelSize: 13
                                            font.bold: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.lastTorrentMagnet = modelData.magnet
                                            bridge.selectTorrent(modelData.magnet)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Озвучки CVH/Kodik — список зависит от выбранного источника
            Column {
                id: translationsColumn
                width: parent.width
                spacing: 8
                readonly property var activeTranslations: {
                    if (root.sourceType === "kodik") return root.kodikTranslations
                    if (root.sourceType === "animetka") return root.animetkaTranslations
                    if (root.sourceType === "hentasis") return root.hentasisTranslations
                    if (root.sourceType === "anistar") return root.anistarTranslations
                    if (root.sourceType === "smash") {
                        // Smash: тот источник у кого больше озвучек
                        return root.cvhTranslations.length >= root.kodikTranslations.length
                            ? root.cvhTranslations : root.kodikTranslations
                    }
                    return root.cvhTranslations
                }
                visible: (root.sourceType === "cvh" || root.sourceType === "kodik" || root.sourceType === "animetka"
                    || root.sourceType === "hentasis"
                    || root.sourceType === "anistar" || root.sourceType === "smash") && activeTranslations.length > 0
                Text { text: "Озвучка"; color: Theme.textSecondary; font.pixelSize: 12 }
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: translationsColumn.activeTranslations
                        delegate: PillButton {
                            text: modelData.title || modelData.name || "?"
                            active: root.translationId === modelData.id
                            onClicked: root.translationId = modelData.id
                        }
                    }
                }
                // Качество — только Animetka (у Kodik качество резолвится иначе)
                Text {
                    text: "Качество"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    visible: root.sourceType === "animetka" && root.currentAnimetkaQualities.length > 0
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    visible: root.sourceType === "animetka" && root.currentAnimetkaQualities.length > 0
                    Repeater {
                        model: root.currentAnimetkaQualities
                        delegate: PillButton {
                            text: modelData + "p"
                            active: root.streamQuality === modelData
                            onClicked: root.streamQuality = modelData
                        }
                    }
                }
            }

            // === Дата выхода (если ни одной озвучки ещё нет — анонс/выходит) ===
            Column {
                width: parent.width
                spacing: 4
                visible: root.translationsLoaded && root.anilibriaChecked && root.translations.length === 0 && !root.anilibriaAvailable
                Text { text: "Дата выхода"; color: Theme.textSecondary; font.pixelSize: 12 }
                Text {
                    text: root.releaseDateText
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            // === Серия + воспроизведение ===
            Row {
                width: parent.width
                spacing: 16
                visible: !!(root.item && root.item.id)

                Column {
                    id: episodeColumn
                    spacing: 4
                    Text { text: "Серия"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Row {
                        id: episodeRow
                        spacing: 6
                        Button {
                            text: "−"
                            implicitWidth: 36; implicitHeight: 36
                            background: Rectangle { radius: Theme.cornerPill; color: Theme.bgPill }
                            contentItem: Text { text: "−"; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: root.episode = Math.max(1, root.episode - 1)
                        }
                        Rectangle {
                            width: 64; height: 36
                            radius: Theme.cornerSmall
                            color: Theme.bgInput
                            border.width: episodeField.acceptableInput ? 0 : 1
                            border.color: Theme.bad

                            TextField {
                                id: episodeField
                                anchors.fill: parent
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                background: Item {}
                                selectByMouse: true
                                inputMethodHints: Qt.ImhDigitsOnly

                                readonly property int maxEpisode: root.currentMaxEpisode
                                // acceptableInput — надёжный сигнал валидности диапазона
                                readonly property bool acceptableInput: {
                                    var v = parseInt(text)
                                    return text.length > 0 && !isNaN(v) && v >= 1 && v <= maxEpisode
                                }

                                // root.episode меняется снаружи (+/- кнопки, плеер) — текст
                                // обновляем программно, а не биндингом (биндинг слетает,
                                // как только пользователь сам печатает в поле)
                                Connections {
                                    target: root
                                    function onEpisodeChanged() { episodeField.text = String(root.episode) }
                                }
                                Component.onCompleted: text = String(root.episode)

                                function commit() {
                                    var v = parseInt(text)
                                    if (isNaN(v)) {
                                        text = String(root.episode)
                                        return
                                    }
                                    root.episode = Math.max(1, Math.min(maxEpisode, v))
                                    text = String(root.episode)
                                }
                                onEditingFinished: commit()
                                Keys.onReturnPressed: commit()
                                Keys.onEnterPressed: commit()
                            }
                        }
                        Button {
                            text: "+"
                            implicitWidth: 36; implicitHeight: 36
                            background: Rectangle { radius: Theme.cornerPill; color: Theme.bgPill }
                            contentItem: Text { text: "+"; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: root.episode = Math.min(root.currentMaxEpisode, root.episode + 1)
                        }
                    }
                    Text {
                        text: "Неверное значение (1–" + episodeField.maxEpisode + ")"
                        color: Theme.bad
                        font.pixelSize: 10
                        visible: !episodeField.acceptableInput
                    }
                }

                Row {
                    y: episodeRow.y
                    spacing: 8

                    Button {
                        id: playBtn
                        text: playback.buffering ? "Загрузка..." : "▶ Смотреть"
                        implicitWidth: 140
                        implicitHeight: 36
                        enabled: root.canPlay && episodeField.acceptableInput && !playback.buffering
                        background: Rectangle { radius: Theme.cornerPill; color: playback.buffering ? Theme.bgCardHover : (playBtn.enabled ? (playBtn.hovered ? Theme.accentLight : Theme.accent) : Theme.bgPill) }
                        contentItem: Text { text: playBtn.text; color: playBtn.enabled || playback.buffering ? "white" : Theme.textMuted; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        // commit() сначала — поле пишет root.episode только по
                        // editingFinished, и при ручном вводе + клике мимо поля
                        // play() иначе прочитал бы старый номер
                        onClicked: {
                            episodeField.commit()
                            bridge.play(root.episode, root.effectiveTranslationId())
                        }
                    }
                }
            }

            Text {
                id: statusText
                color: Theme.bad
                font.pixelSize: 12
            }

            // Слот в потоке страницы — в полноэкранном режиме пустой, плеер в отдельном окне
            Item {
                id: inlinePlayerSlot
                width: parent.width
                height: root.showPlayerPanel ? 420 : 0
                visible: height > 0
                Behavior on height { NumberAnimation { duration: 150 } }
            }

            // === Связанное (Шикимори: сиквелы/приквелы/адаптации и т.п.) ===
            Column {
                width: parent.width
                spacing: 8
                visible: root.relatedModel.length > 0
                Text { text: "Связанное"; color: Theme.textSecondary; font.pixelSize: 12 }

                Flickable {
                    id: relatedFlick
                    width: parent.width
                    height: 230
                    contentWidth: relatedRow.width
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: relatedRow
                        spacing: 10
                        Repeater {
                            model: root.relatedModel
                            delegate: Column {
                                spacing: 4
                                Card {
                                    anime: modelData
                                    onClicked: {
                                        // StackView не уничтожает этот экран при push нового
                                        // поверх — без явной остановки плеер продолжал бы
                                        // играть текущий тайтл в фоне под новым DetailView.
                                        root.cinemaMode = false
                                        player.stop()
                                        root.openDetail(modelData)
                                    }
                                    onGenreClicked: function(genreId) { root.genreClicked(genreId) }
                                    onYearClicked: function(year) { root.yearClicked(year) }
                                }
                                Text {
                                    width: 160
                                    text: modelData.relation || ""
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }

    // Плеер: встроенный слот или слой на весь экран (Main.qml переводит окно в FullScreen)
    Item {
        id: playerHost
        parent: root.cinemaMode ? cinemaLayer : inlinePlayerSlot
        anchors.fill: parent
        clip: true

        readonly property bool playerActive: player.hasMedia || player.loading || playback.buffering

        onPlayerActiveChanged: root.showPlayerPanel = playerActive
        Component.onCompleted: root.showPlayerPanel = playerActive

        Connections {
            target: root
            function onCinemaModeChanged() {
                if (root.cinemaMode)
                    Qt.callLater(function() { cinemaLayer.forceActiveFocus() })
            }
        }

        MpvPlayer {
            id: player
            anchors.fill: parent
            layer.enabled: hasMedia
            layer.smooth: false
            Component.onCompleted: playback.attachPlayer(player)
            Component.onDestruction: {
                root.cinemaMode = false
                player.stop()
            }
        }

        PlayerOverlay {
            anchors.fill: parent
            player: player
            playback: playback
            titleText: root.item.title || ""
            maxEpisode: root.currentMaxEpisode
            cinemaMode: root.cinemaMode
            onRequestCinemaToggle: root.cinemaMode = !root.cinemaMode
            onRequestEpisode: function(ep) {
                if (ep < 1 || ep > root.currentMaxEpisode) return
                root.episode = ep
                bridge.play(ep, root.effectiveTranslationId())
            }
        }

        Rectangle {
            anchors.centerIn: parent
            visible: playback.buffering
            width: 260; height: 60
            radius: 8
            color: "#202020cc"
            Text {
                anchors.centerIn: parent
                color: "white"
                text: playback.statusMessage || "Буферизация..."
            }
        }

        // Файловый пикер — показывается когда TorrServer не смог найти серию
        // автоматически (например, раздача с бонус-папками/несколькими сезонами).
        Rectangle {
            id: torrentFilePicker
            property var files: []
            visible: false
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.85, 620)
            height: Math.min(pickerColumn.implicitHeight + 24, parent.height * 0.7)
            radius: 10
            color: "#1e1e2e"
            clip: true

            Column {
                id: pickerColumn
                width: parent.width
                padding: 16
                spacing: 10

                Text {
                    text: "Выбери файл для воспроизведения"
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                }

                Flickable {
                    width: parent.width - 32
                    height: Math.min(torrentFilePicker.files.length * 56, torrentFilePicker.height - 80)
                    contentWidth: width
                    contentHeight: fileCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: fileCol
                        width: parent.width
                        spacing: 4

                        Repeater {
                            model: torrentFilePicker.files
                            delegate: Rectangle {
                                width: fileCol.width
                                height: 48
                                radius: 6
                                color: fileHover.containsMouse ? "#2a2a4a" : "#15152a"

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 10
                                    spacing: 2
                                    Text {
                                        width: parent.width
                                        text: modelData.name
                                        color: "white"
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: modelData.sizeMb
                                        color: "#888"
                                        font.pixelSize: 10
                                    }
                                }

                                MouseArea {
                                    id: fileHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        torrentFilePicker.visible = false
                                        playback.playTorrentFile(modelData.hash, modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                Button {
                    text: "Отмена"
                    onClicked: torrentFilePicker.visible = false
                }
            }
        }

        Rectangle {
            id: resumePrompt
            property int episode: 1
            property double positionSeconds: 0
            anchors.centerIn: parent
            visible: false
            width: 320; height: 110
            radius: 8
            color: "#202020e6"

            // Крестик закрытия — на случай если подсказка "продолжить"
            // относится к уже неактуальной серии (например, всплыла для
            // предыдущей серии, а пользователь уже переключился на
            // следующую) и предложенные варианты ("Продолжить"/"С начала")
            // тут просто не при делах.
            Text {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 6
                text: "✕"
                color: "white"
                opacity: closeMouse.containsMouse ? 1 : 0.6
                font.pixelSize: 14
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: resumePrompt.visible = false
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                Text {
                    color: "white"
                    text: "Продолжить серию " + resumePrompt.episode + " с "
                        + Math.floor(resumePrompt.positionSeconds / 60) + ":"
                        + String(Math.floor(resumePrompt.positionSeconds % 60)).padStart(2, "0") + "?"
                }
                Row {
                    spacing: 8
                    Button {
                        text: "Продолжить"
                        onClicked: {
                            playback.applyResumePosition(resumePrompt.positionSeconds)
                            resumePrompt.visible = false
                        }
                    }
                    Button {
                        text: "С начала"
                        onClicked: resumePrompt.visible = false
                    }
                }
            }
        }

    }
}

