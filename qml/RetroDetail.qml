import QtQuick
import AnimeClient 1.0
import QtQuick.Effects
import QtQuick.Window

// Порт components/detail-screen.tsx — реальные метаданные/переводы/статус
// через DetailBridge, реальное воспроизведение через PlaybackController+
// MpvPlayer (тот же C++, что у обычной DetailView.qml). Источники (CVH/
// Kodik/Hentasis/AniStar/AniLibria/торренты/Smash), список переводов по
// источнику и выбор файла торрента вручную — та же логика 1:1, что и в
// DetailView.qml, просто в терминальном виде.
//
// root — ОБЫЧНЫЙ Item, а не Flickable: прокручиваемый контент вынесен во
// вложенный detailFlick. Если сделать сам root Flickable-ом (как было
// раньше), то cinemaLayer/playerHost, объявленные "на верхнем уровне",
// автоматически становятся частью его прокручиваемого contentItem — и их
// anchors.fill: parent привязывается не к реальному размеру окна, а к
// contentHeight (сумме высот блоков страницы), из-за чего полноэкранный
// плеер получал случайную маленькую высоту вместо всего окна. Тот же
// приём (root = Item, Flickable — вложенный), что в DetailView.qml.
Item {
    id: root

    property var item: ({})
    property Window appWindow: null
    signal openAnime(var item)
    signal backRequested()

    // Полноэкранный плеер — тот же приём, что в DetailView.qml (обычная
    // тема): плеер не создаётся заново в отдельном Window (это ломает
    // OpenGL-контекст mpv), вместо этого один и тот же playerHost
    // репарентится между обычным слотом в потоке страницы и cinemaLayer
    // (слой на весь экран). Тайтлбар/сайдбар в RetroShell.qml прячутся по
    // этому же флагу — иначе они остаются поверх видео.
    property bool cinemaMode: false
    onCinemaModeChanged: {
        if (root.appWindow)
            root.appWindow.visibility = root.cinemaMode ? Window.FullScreen : Window.Windowed
    }

    property var details: ({})
    property var translations: []
    property bool translationsLoaded: false
    property bool anilibriaAvailable: false
    property bool anilibriaChecked: false
    property int anilibriaEpisodes: 0
    property var torrents: []
    property bool torrentsLoading: false
    property bool torrentsLoaded: false
    property string lastTorrentMagnet: ""
    // "" | cvh | kodik | animetka | hentasis | anistar | anilibria | torrent | smash
    property string sourceType: ""
    property var relatedModel: []
    property int episode: 1
    property string translationId: ""
    property string streamQuality: ""
    property string _loadedId: ""
    property double _pendingAudioSyncOffset: 0

    // === Разбивка общего списка переводов по источникам — DetailBridge
    // отдаёт CVH+Kodik+Hentasis+AniStar вперемешку одним списком, различить
    // можно только по префиксу id (см. DetailView.qml — 1:1 та же логика). ===
    function isCvhTranslationId(id) { return id !== "" && id.indexOf("cvh_") === 0 }
    function isKodikTranslationId(id) { return id !== "" && id.indexOf("kodik_") === 0 }
    function isAnimetkaTranslationId(id) { return id !== "" && id.indexOf("animetka_") === 0 }
    function isHentasisTranslationId(id) { return id !== "" && id.indexOf("hentasis_") === 0 }
    function isAnistarTranslationId(id) { return id !== "" && id.indexOf("anistar_") === 0 }
    function isHentaiItem() {
        const tags = root.details.genreTags || []
        for (let i = 0; i < tags.length; i++)
            if (tags[i].id === 12) return true
        return false
    }
    readonly property bool hentaiItem: root.isHentaiItem()

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
    readonly property bool torrentSourceVisible: root.hentaiItem || root.torrentsLoading
        || root.lastTorrentMagnet !== "" || (root.torrentsLoaded && root.torrents.length > 0)
    readonly property bool torrentSourceEnabled: root.lastTorrentMagnet !== ""
        || (root.torrentsLoaded && !root.torrentsLoading && root.torrents.length > 0)

    readonly property var currentAnimetkaQualities: {
        if (!root.isAnimetkaTranslationId(root.translationId)) return []
        for (let i = 0; i < root.animetkaTranslations.length; i++) {
            if (root.animetkaTranslations[i].id === root.translationId) {
                const q = root.animetkaTranslations[i].qualities || []
                return q.slice().sort(function(a, b) { return parseInt(b) - parseInt(a) })
            }
        }
        return []
    }
    function syncQualityForTranslation() {
        if (root.sourceType !== "animetka") { root.streamQuality = ""; return }
        const qs = root.currentAnimetkaQualities
        if (qs.length === 0) { root.streamQuality = ""; return }
        if (root.streamQuality !== "" && qs.indexOf(root.streamQuality) >= 0) return
        let best = qs[0]
        for (let i = 0; i < qs.length; i++)
            if (parseInt(qs[i]) > parseInt(best)) best = qs[i]
        root.streamQuality = best
    }

    function effectiveTranslationId() {
        if (root.sourceType === "anilibria") return "anilibria"
        if (root.sourceType === "torrent") return "torrent"
        if (root.sourceType === "cvh" || root.sourceType === "kodik"
            || root.sourceType === "hentasis" || root.sourceType === "anistar") return root.translationId
        if (root.sourceType === "animetka") {
            if (root.streamQuality && root.streamQuality !== "" && root.streamQuality !== "best") {
                let base = root.translationId
                const qpos = base.indexOf("_q")
                if (qpos > 0) base = base.substring(0, qpos)
                return base + "_q" + root.streamQuality
            }
            return root.translationId
        }
        if (root.sourceType === "smash") return root.translationId
        return ""
    }

    // Smash: авто-выбор источника озвучки с бОльшим числом вариантов (CVH/Kodik),
    // но сначала — последняя реально использованная связка для этого тайтла,
    // если она всё ещё доступна среди текущих переводов.
    function smashBestVoiceId() {
        const titleId = root.item && root.item.id ? root.item.id : ""
        if (titleId) {
            const saved = appConfig.smashChoice(titleId)
            if (saved) {
                if (root.isCvhTranslationId(saved) && root.cvhTranslations.some(function(t) { return t.id === saved }))
                    return saved
                if (root.isKodikTranslationId(saved) && root.kodikTranslations.some(function(t) { return t.id === saved }))
                    return saved
            }
        }
        const src = root.cvhTranslations.length >= root.kodikTranslations.length ? root.cvhTranslations : root.kodikTranslations
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
            const bestId = root.smashBestVoiceId()
            if (bestId !== "") root.translationId = bestId
            if (root.lastTorrentMagnet === "" && root.torrents.length > 0)
                bridge.selectTorrent(root.torrents[0].magnet)
        }
    }

    function applySavedSource(translationIdArg) {
        if (root.isCvhTranslationId(translationIdArg)) { root.sourceType = "cvh"; root.translationId = translationIdArg }
        else if (root.isKodikTranslationId(translationIdArg)) { root.sourceType = "kodik"; root.translationId = translationIdArg }
        else if (root.isAnimetkaTranslationId(translationIdArg)) {
            root.sourceType = "animetka"
            let base = translationIdArg
            const qpos = base.indexOf("_q")
            if (qpos > 0) {
                root.streamQuality = base.substring(qpos + 2)
                base = base.substring(0, qpos)
            }
            root.translationId = base
        }
        else if (root.isHentasisTranslationId(translationIdArg)) { root.sourceType = "hentasis"; root.translationId = translationIdArg }
        else if (root.isAnistarTranslationId(translationIdArg)) { root.sourceType = "anistar"; root.translationId = translationIdArg }
        else if (translationIdArg === "anilibria") { root.sourceType = "anilibria"; root.translationId = "anilibria" }
        else if (translationIdArg === "torrent") {
            // Смэш-микс (видео с торрента + звук с Kodik/CVH) в HistoryManager
            // сохраняется как обычный "torrent" — реальная озвучка помнится
            // отдельно в appConfig.smashChoice. Различаем по её наличию.
            const titleId = root.item && root.item.id ? root.item.id : ""
            const savedAudio = titleId ? appConfig.smashChoice(titleId) : ""
            if (savedAudio && (root.isCvhTranslationId(savedAudio) || root.isKodikTranslationId(savedAudio))) {
                root.sourceType = "smash"
                root.translationId = savedAudio
            } else {
                root.sourceType = "torrent"
                root.translationId = "torrent"
            }
        }
    }

    readonly property bool canPlay: {
        if (root.sourceType === "cvh") return root.cvhAvailable && root.isCvhTranslationId(root.translationId)
        if (root.sourceType === "kodik") return root.kodikAvailable && root.isKodikTranslationId(root.translationId)
        if (root.sourceType === "animetka") return root.animetkaAvailable && root.isAnimetkaTranslationId(root.translationId)
        if (root.sourceType === "hentasis") return root.hentasisAvailable && root.isHentasisTranslationId(root.translationId)
        if (root.sourceType === "anistar") return root.anistarAvailable && root.isAnistarTranslationId(root.translationId)
        if (root.sourceType === "anilibria") return root.anilibriaSourceAvailable
        if (root.sourceType === "torrent") return root.torrentSourceEnabled && root.lastTorrentMagnet !== ""
        if (root.sourceType === "smash")
            return (root.isCvhTranslationId(root.translationId) || root.isKodikTranslationId(root.translationId)) || root.lastTorrentMagnet !== ""
        return false
    }

    // у выбранной озвучки серий часто меньше, чем у тайтла на Шикимори —
    // поле "Серия" должно ограничиваться именно этим числом.
    // details.episodes — общее число серий, 0 пока тайтл онгоинг (Шикимори
    // ещё не знает финал) — тогда ограничиваемся episodesAired (сколько
    // реально вышло), иначе двигаться по сериям было некуда (заглушка 9999).
    readonly property int knownEpisodeCount: root.details.episodes || root.details.episodesAired || 9999
    readonly property int maxEpisode: {
        if (root.sourceType === "anilibria")
            return root.anilibriaEpisodes > 0 ? root.anilibriaEpisodes : root.knownEpisodeCount
        if (root.sourceType === "cvh" || root.sourceType === "kodik"
            || root.sourceType === "animetka"
            || root.sourceType === "hentasis" || root.sourceType === "anistar") {
            for (let i = 0; i < root.translations.length; i++)
                if (root.translations[i].id === root.translationId && root.translations[i].episodes > 0)
                    return root.translations[i].episodes
        }
        return root.knownEpisodeCount
    }

    function setEp(n) { root.episode = Math.max(1, Math.min(root.maxEpisode, n)) }

    // TextInput.text: root.episode — это только начальный биндинг: как
    // только пользователь один раз отредактирует поле "EP", QML рвёт связь
    // и оно перестаёт обновляться от кнопок плеера/автопереключения серии.
    // Пересинхронизируем принудительно при каждом изменении root.episode.
    onEpisodeChanged: if (epSpin) epSpin.text = root.episode

    // Единая точка переключения серии из плеера (кнопки prev/next, авто-
    // переключение по окончании серии) — сразу запускает воспроизведение,
    // в отличие от постраничного PREV/NEXT/поля EP, которые только готовят
    // номер серии к ручному запуску кнопкой PLAY.
    function playEpisode(ep) {
        root.episode = Math.max(1, Math.min(root.maxEpisode, ep))
        if (!root.canPlay) return
        if (root.sourceType === "smash") {
            // Видео — с торрента (лучше качество, чем прямые потоки Kodik/CVH),
            // звук — с выбранной озвучки поверх него.
            const audioId = (root.isCvhTranslationId(root.translationId) || root.isKodikTranslationId(root.translationId))
                ? root.translationId : ""
            if (audioId !== "" && root.item && root.item.id)
                appConfig.setSmashChoice(root.item.id, audioId)
            if (root.lastTorrentMagnet !== "") {
                bridge.playSmashMixed(root.episode, audioId)
                // audioDelay сбрасывается в 0 при загрузке нового видеофайла
                // (playUrl), а видео с торрента грузится асинхронно — ставим
                // сохранённый пресет не сразу, а как только реально появится
                // медиа (см. Connections на player.hasMediaChanged ниже).
                if (root.item && root.item.id)
                    root._pendingAudioSyncOffset = appConfig.audioSyncOffset(root.item.id)
            }
            else if (audioId !== "")
                bridge.play(root.episode, audioId)
            return
        }
        bridge.play(root.episode, root.effectiveTranslationId())
    }

    readonly property string posterSource: {
        const hd = root.details.posterHd || root.item.posterHd || ""
        const p = root.details.poster || root.item.poster || ""
        function ok(u) { return u && u.length > 0 && u.indexOf("/missing_") < 0 }
        if (ok(hd)) return hd
        if (ok(p)) return p
        return hd || p
    }

    // Широкий hero-баннер для фона шапки (тот же, что на Главной) — постер
    // 2:3 растянутый на всю ширину сеткой давал дублирующийся искажённый
    // кадр (тот же кадр ещё раз показан нормальными пропорциями в рамке
    // постера рядом). Баннер приходит асинхронно (AniList, после основного
    // detailsReady) — тот же паттерн кэша/приоритета, что в RetroHome.qml.
    property string heroDisplaySource: ""
    readonly property string heroBannerUrl: (root.details && root.details.heroBanner) ? String(root.details.heroBanner) : ""
    function posterOk(u) { return u && u.length > 0 && u.indexOf("/missing_") < 0 }

    function refreshHeroImage() {
        if (!posterOk(root.heroBannerUrl)) {
            root.heroDisplaySource = ""
            return
        }
        const cached = posterCache.cachedFile(root.heroBannerUrl)
        if (cached.length > 0) {
            root.heroDisplaySource = cached
            return
        }
        if (posterCache.isRemotePoster(root.heroBannerUrl))
            posterCache.requestPriority(root.heroBannerUrl)
        root.heroDisplaySource = appConfig.proxyEnabled ? "" : root.heroBannerUrl
    }
    onHeroBannerUrlChanged: refreshHeroImage()

    Connections {
        target: posterCache
        function onPosterReady(remoteUrl, fileUrl) {
            if (remoteUrl === root.heroBannerUrl)
                root.heroDisplaySource = fileUrl
        }
    }

    PlaybackController {
        id: playback
        onErrorOccurred: function(msg) { statusText.text = msg }
        onEpisodeChanged: {
            if (playback.currentEpisode > 0)
                root.episode = playback.currentEpisode
        }
        onNextEpisodeNeeded: function(ep) { root.playEpisode(ep) }
        // Раздача с несколькими файлами (бонусы/несколько сезонов в одной
        // торрент-папке) — TorrServer не может угадать нужный сам, просит
        // выбрать вручную.
        onTorrentFilesReady: function(files) {
            torrentFilePicker.files = files
            torrentFilePicker.visible = true
        }
    }

    // audioDelay сбрасывается в 0 при каждой новой загрузке видео (playUrl) —
    // сохранённый пресет сдвига звука (смэш) применяем именно тогда, когда
    // видео реально появилось, а не сразу по клику PLAY.
    Connections {
        target: player
        function onHasMediaChanged() {
            if (player.hasMedia && root._pendingAudioSyncOffset !== 0) {
                player.audioDelay = root._pendingAudioSyncOffset
                root._pendingAudioSyncOffset = 0
            }
        }
    }

    DetailBridge {
        id: bridge
        Component.onCompleted: attachPlaybackController(playback)
        onDetailsReady: function(d) { root.details = d }
        onProgressReady: function(ep, translationIdArg, torrentMagnet) {
            root.episode = ep
            root.lastTorrentMagnet = torrentMagnet || ""
            if (translationIdArg)
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
            if (root.sourceType === "") {
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
            playback.setTotalEpisodes(root.maxEpisode)
        }
        onAnilibriaReady: function(ok) {
            root.anilibriaAvailable = ok
            root.anilibriaChecked = true
        }
        onAnilibriaEpisodesReady: function(count) {
            root.anilibriaEpisodes = count
            if (root.sourceType === "anilibria")
                playback.setTotalEpisodes(root.maxEpisode)
        }
        onRelatedReady: function(items) { root.relatedModel = items }
        onError: function(msg) { statusText.text = msg }
        onTorrentsReady: function(list) {
            root.torrents = list
            root.torrentsLoaded = true
            root.torrentsLoading = false
            if (root.hentaiItem && list.length > 0 && root.sourceType === ""
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

    function loadCurrentItem() {
        if (!root.item || !root.item.id || root.item.id === root._loadedId)
            return
        // Переход на другой тайтл (напр. клик по "Связанное") переиспользует
        // тот же экземпляр RetroDetail — Loader выше не пересоздаёт компонент,
        // пока screen остаётся "detail". Без явной остановки плеер продолжал
        // играть предыдущий тайтл в фоне, пока UI уже показывал новый.
        root.cinemaMode = false
        player.stop()
        root._loadedId = root.item.id
        root.details = root.item
        root.translations = []
        root.translationsLoaded = false
        root.anilibriaAvailable = false
        root.anilibriaChecked = false
        root.anilibriaEpisodes = 0
        root.torrents = []
        root.torrentsLoaded = false
        root.torrentsLoading = false
        root.lastTorrentMagnet = ""
        root.sourceType = ""
        root.translationId = ""
        root.relatedModel = []
        root.episode = 1
        statusText.text = ""
        bridge.load(root.item)
    }
    onItemChanged: loadCurrentItem()
    Component.onCompleted: loadCurrentItem()

    Flickable {
        id: detailFlick
        anchors.fill: parent
        visible: !root.cinemaMode
        contentWidth: width
        contentHeight: contentColumn.height
        clip: true

    Column {
        id: contentColumn
        width: detailFlick.width
        spacing: 0

        // === HEADER: постер тайтла как размытый фон + сам постер поверх ===
        Item {
            id: headerBlock
            width: parent.width
            height: Math.max(300, 300 - 190 + infoRow.height)

            Item {
                id: bgArea
                width: parent.width
                height: 300
                clip: true

                Rectangle { anchors.fill: parent; color: RetroTheme.background }

                Image {
                    anchors.fill: parent
                    visible: root.heroDisplaySource.length > 0
                    source: root.heroDisplaySource
                    sourceSize: Qt.size(2560, 1440)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    cache: false
                    opacity: 0.35
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: RetroTheme.background }
                        GradientStop { position: 0.5; color: Qt.rgba(0.024, 0.031, 0.027, 0.7) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.031, 0.027, 0.3) }
                    }
                }
                ScanlinesOverlay { anchors.fill: parent }
            }

            Rectangle {
                x: 16; y: 16
                width: backRow.width + 24
                height: 30
                color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
                border.width: 1
                border.color: backMouse.containsMouse ? RetroTheme.primary : RetroTheme.border
                z: 3

                Row {
                    id: backRow
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "←"; font.pixelSize: 12; color: backMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                    Text {
                        text: "BACK"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 10
                        color: backMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground
                    }
                }
                MouseArea {
                    id: backMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }
            }

            Row {
                id: infoRow
                x: 20
                y: 300 - 190
                width: parent.width - 40
                spacing: 20
                z: 2

                Rectangle {
                    id: posterBox
                    width: 190
                    height: 190 * 1.5
                    color: RetroTheme.card
                    border.width: 1
                    border.color: RetroTheme.primary

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: RetroTheme.primary
                        shadowBlur: 0.5
                        shadowOpacity: 0.35
                    }

                    PosterThumbnail {
                        anchors.fill: parent
                        anchors.margins: 1
                        posterSource: root.posterSource
                        posterActive: true
                        cornerRadius: 0
                        placeholderLetter: (root.details.title || "?").trim().charAt(0).toUpperCase() || "?"
                    }
                    Rectangle {
                        visible: (root.details.score || 0) > 0
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 8
                        width: posterRating.width + 10
                        height: 18
                        color: Qt.rgba(0.024, 0.031, 0.027, 0.8)
                        border.width: 1
                        border.color: Qt.rgba(1, 0.702, 0.169, 0.7)
                        Row {
                            id: posterRating
                            anchors.centerIn: parent
                            spacing: 3
                            Text { text: "★"; color: RetroTheme.accent; font.pixelSize: 9 }
                            Text {
                                text: Number(root.details.score || 0).toFixed(1)
                                color: RetroTheme.accent
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                Column {
                    width: parent.width - posterBox.width - parent.spacing
                    topPadding: 100
                    spacing: 12

                    Column {
                        width: parent.width
                        spacing: 2
                        Text {
                            text: [
                                (root.details.status || "").toString().toUpperCase(),
                                root.details.year || "",
                                (root.details.episodes ? root.details.episodes + " EPISODES" : "")
                            ].filter(function(s) { return s !== "" && s !== 0 }).join(" · ")
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 10
                            color: RetroTheme.accent
                        }
                        Text {
                            text: root.details.title || ""
                            font.family: RetroTheme.fontFamily
                            font.bold: true
                            font.pixelSize: 24
                            color: RetroTheme.primary
                            wrapMode: Text.WordWrap
                            width: parent.width
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                shadowEnabled: true
                                shadowColor: RetroTheme.primary
                                shadowBlur: 0.7
                                shadowOpacity: 0.6
                            }
                        }
                    }

                    Text {
                        id: statusText
                        width: Math.min(parent.width, 640)
                        text: root.details.description || ""
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 12
                        color: Qt.rgba(0.812, 0.910, 0.824, 0.85)
                        wrapMode: Text.WordWrap
                    }

                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: root.details.genreTags || []
                            delegate: Rectangle {
                                width: genreTag.width + 14
                                height: 22
                                color: Qt.rgba(0.220, 0.973, 0.573, 0.1)
                                border.width: 1
                                border.color: Qt.rgba(0.220, 0.973, 0.573, 0.5)
                                Text {
                                    id: genreTag
                                    anchors.centerIn: parent
                                    text: "#" + (modelData.name || "").toUpperCase()
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 9
                                    color: RetroTheme.primary
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 10
                        Rectangle {
                            width: playRow.width + 32
                            height: 36
                            enabled: root.canPlay
                            opacity: enabled ? 1 : 0.4
                            color: playMouse.containsMouse ? "transparent" : RetroTheme.primary
                            border.width: 1
                            border.color: RetroTheme.primary
                            Behavior on color { ColorAnimation { duration: 120 } }
                            layer.enabled: playMouse.containsMouse
                            layer.effect: MultiEffect {
                                shadowEnabled: true
                                shadowColor: RetroTheme.primary
                                shadowBlur: 0.6
                                shadowOpacity: 0.5
                            }
                            Row {
                                id: playRow
                                anchors.centerIn: parent
                                spacing: 8
                                Text { text: "▶"; font.pixelSize: 11; color: playMouse.containsMouse ? RetroTheme.primary : RetroTheme.primaryForeground }
                                Text {
                                    text: playback.buffering ? "LOADING..." : "PLAY EP " + (root.episode < 10 ? "0" : "") + root.episode
                                    font.family: RetroTheme.fontFamily
                                    font.bold: true
                                    font.pixelSize: 11
                                    color: playMouse.containsMouse ? RetroTheme.primary : RetroTheme.primaryForeground
                                }
                            }
                            MouseArea {
                                id: playMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.playEpisode(root.episode)
                            }
                        }

                    }

                    // === СТАТУС — раньше тут была одна кнопка BOOKMARK,
                    // жёстко переключавшая только "watching"/"" — добавить в
                    // "В планах"/"Просмотрено"/"Брошено"/"Отложено" было
                    // просто нечем. Пять раздельных пилюль, как в обычной
                    // теме (DetailView.qml — StatusPill), одна активна за раз.
                    Column {
                        width: parent.width
                        spacing: 6
                        Text {
                            text: "STATUS"
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 9
                            color: RetroTheme.mutedForeground
                        }
                        Flow {
                            width: parent.width
                            spacing: 6

                            component StatusChip: Rectangle {
                                id: statusChip
                                property string label: ""
                                property string value: ""
                                readonly property bool active: bridge.currentStatus === statusChip.value
                                width: statusChipText.width + 20
                                height: 30
                                color: statusChip.active ? RetroTheme.accent : (statusChipMouse.containsMouse ? Qt.rgba(1, 0.702, 0.169, 0.12) : RetroTheme.card)
                                border.width: 1
                                border.color: statusChip.active ? RetroTheme.accent : RetroTheme.border
                                Text {
                                    id: statusChipText
                                    anchors.centerIn: parent
                                    text: statusChip.label
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 10
                                    color: statusChip.active ? RetroTheme.background : RetroTheme.foreground
                                }
                                MouseArea {
                                    id: statusChipMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // Повторный клик по уже активному статусу снимает его.
                                    onClicked: bridge.setStatus(statusChip.active ? "" : statusChip.value)
                                }
                            }

                            StatusChip { label: "СМОТРЮ"; value: "watching" }
                            StatusChip { label: "ПРОСМОТРЕНО"; value: "watched" }
                            StatusChip { label: "БРОШЕНО"; value: "dropped" }
                            StatusChip { label: "В ПЛАНАХ"; value: "planned" }
                            StatusChip { label: "ОТЛОЖЕНО"; value: "postponed" }
                        }
                    }
                }
            }
        }

        // === СЛОТ ПЛЕЕРА В ПОТОКЕ СТРАНИЦЫ — просто резервирует место; сам
        // плеер (playerHost, объявлен ниже вне contentColumn) сюда
        // репарентится, когда не активен cinemaMode. ===
        Item {
            id: inlinePlayerSlot
            width: parent.width
            // Фиксированная высота (как в обычной теме — inlinePlayerSlot
            // там тоже 420), а не width*9/16: при полной ширине карточки
            // видео получалось огромным. mpv сам вписывает картинку с
            // сохранением пропорций (леттербокс/пилларбокс при не строго
            // 16:9 контейнере), так что фиксированная высота не искажает.
            height: (!root.cinemaMode && playerHost.playerActive) ? 420 : 0
            visible: height > 0
        }

        // === PLAYER CONTROLS ===
        Column {
            x: 20
            width: parent.width - 40
            topPadding: 24
            spacing: 14

            component SourceChip: Rectangle {
                id: chip
                property string label: ""
                property bool active: false
                property bool chipEnabled: true
                signal clicked()
                width: chipText.width + 20
                height: 30
                visible: true
                opacity: chipEnabled ? 1 : 0.35
                color: active ? RetroTheme.primary : (chipMouse.containsMouse && chipEnabled ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : RetroTheme.card)
                border.width: 1
                border.color: active ? RetroTheme.primary : RetroTheme.border
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.label
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: chip.active ? RetroTheme.primaryForeground : RetroTheme.foreground
                }
                MouseArea {
                    id: chipMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: chip.chipEnabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chip.clicked()
                }
            }

            // === Источник (порт блока "Источник" из DetailView.qml) ===
            Column {
                width: parent.width
                spacing: 8
                visible: !!(root.details && root.details.id)

                Text {
                    text: "SOURCE"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 9
                    color: RetroTheme.mutedForeground
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    SourceChip {
                        label: "HENTASIS"
                        visible: root.hentaiItem
                        active: root.sourceType === "hentasis"
                        chipEnabled: root.hentasisAvailable
                        onClicked: root.selectSource("hentasis")
                    }
                    SourceChip {
                        label: "ANISTAR"
                        visible: root.hentaiItem
                        active: root.sourceType === "anistar"
                        chipEnabled: root.anistarAvailable
                        onClicked: root.selectSource("anistar")
                    }
                    SourceChip {
                        label: "CVH"
                        visible: !root.hentaiItem
                        active: root.sourceType === "cvh"
                        chipEnabled: root.cvhAvailable
                        onClicked: root.selectSource("cvh")
                    }
                    SourceChip {
                        label: "KODIK"
                        active: root.sourceType === "kodik"
                        chipEnabled: root.kodikAvailable
                        onClicked: root.selectSource("kodik")
                    }
                    SourceChip {
                        label: "ANIMETKA"
                        active: root.sourceType === "animetka"
                        chipEnabled: root.animetkaAvailable
                        onClicked: root.selectSource("animetka")
                    }
                    SourceChip {
                        label: "ANILIBRIA"
                        visible: !root.hentaiItem
                        active: root.sourceType === "anilibria"
                        chipEnabled: root.anilibriaSourceAvailable
                        onClicked: root.selectSource("anilibria")
                    }
                    SourceChip {
                        label: root.torrentsLoading ? "TORRENT …" : "TORRENT"
                        visible: root.torrentSourceVisible
                        active: root.sourceType === "torrent"
                        chipEnabled: root.torrentSourceEnabled
                        onClicked: root.selectSource("torrent")
                    }
                    SourceChip {
                        label: "SMASH 💥"
                        visible: !root.hentaiItem && root.torrentSourceVisible && (root.cvhAvailable || root.kodikAvailable)
                        active: root.sourceType === "smash"
                        chipEnabled: (root.cvhAvailable || root.kodikAvailable) && root.torrentSourceEnabled
                        onClicked: root.selectSource("smash")
                    }
                }

                Text {
                    visible: root.hentaiItem && !root.translationsLoaded
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "// ищем потоки на Hentasis / AniStar / Kodik..."
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.mutedForeground
                }
                Text {
                    visible: root.sourceType === "anilibria" && root.anilibriaChecked
                    text: root.anilibriaAvailable
                        ? "// в каталоге AniLibria" + (root.anilibriaEpisodes > 0 ? (" — " + root.anilibriaEpisodes + " серий") : "")
                        : "// нет в каталоге AniLibria"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: root.anilibriaAvailable ? RetroTheme.mutedForeground : RetroTheme.destructive
                }
                Text {
                    // «Не найдено/ошибка» — только после завершения загрузки
                    // источника (state empty/error), иначе сообщение мигает:
                    // translationsLoaded приходит от другого источника раньше.
                    visible: root.sourceType === "cvh" && root.translationsLoaded && !root.cvhAvailable
                        && (!bridge.sourceStatus["cvh"] || bridge.sourceStatus["cvh"].state !== "loading")
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        var st = bridge.sourceStatus["cvh"]
                        if (st && st.state === "error")
                            return "// CVH: ошибка — " + (st.message || "источник недоступен")
                        return "// CVH: озвучки не найдены на AnimeGO для этого тайтла"
                    }
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.destructive
                }
                Text {
                    visible: root.sourceType === "animetka" && root.translationsLoaded && !root.animetkaAvailable
                        && (!bridge.sourceStatus["animetka"] || bridge.sourceStatus["animetka"].state !== "loading")
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        var st = bridge.sourceStatus["animetka"]
                        if (st && st.state === "error")
                            return "// Animetka: ошибка — " + (st.message || "источник недоступен")
                        return "// Animetka: тайтл не найден или API недоступен"
                    }
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.destructive
                }
                Text {
                    visible: root.sourceType === "kodik" && root.translationsLoaded && !root.kodikAvailable
                        && (!bridge.sourceStatus["kodik"] || bridge.sourceStatus["kodik"].state !== "loading")
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        var st = bridge.sourceStatus["kodik"]
                        if (st && st.state === "error")
                            return "// Kodik: ошибка — " + (st.message || "источник недоступен") + " (нужен прокси?)"
                        return "// Kodik: озвучки не найдены (см. Настройки → прокси)"
                    }
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.destructive
                }

                // Список раздач — сразу под чипом Torrent/Smash.
                Column {
                    width: parent.width
                    spacing: 8
                    visible: root.sourceType === "torrent" || root.sourceType === "smash"

                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: root.torrentsLoading
                            ? "// ищу торренты..."
                            : (root.torrents.length > 0
                                ? "// найдено: " + root.torrents.length
                                : (root.torrentsLoaded ? "// ничего не найдено — проверь JacRed/прокси в настройках" : ""))
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 10
                        color: RetroTheme.mutedForeground
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
                        // драг), а прокручивает страницу целиком.
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
                            spacing: 8

                            Repeater {
                                model: root.torrents
                                delegate: Rectangle {
                                    readonly property bool isSelected: (root.sourceType === "torrent" || root.sourceType === "smash")
                                        && modelData.magnet === root.lastTorrentMagnet
                                    width: torrentsList.width
                                    height: torrentBody.height + 20
                                    color: isSelected ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : RetroTheme.card
                                    border.width: isSelected ? 2 : 1
                                    border.color: isSelected ? RetroTheme.primary : RetroTheme.border

                                    Row {
                                        id: torrentBody
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 10
                                        spacing: 10

                                        Column {
                                            width: parent.width - (isSelectedBadge.visible ? isSelectedBadge.width + parent.spacing : 0)
                                            spacing: 4
                                            Text {
                                                width: parent.width
                                                text: modelData.title || "?"
                                                font.family: RetroTheme.fontFamily
                                                font.bold: true
                                                font.pixelSize: 11
                                                color: RetroTheme.foreground
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 3
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                width: parent.width
                                                text: [modelData.tracker, modelData.size, modelData.seeders !== undefined ? "сидов: " + modelData.seeders : ""].filter(function(s) { return s }).join("  ·  ")
                                                font.family: RetroTheme.fontFamily
                                                font.pixelSize: 9
                                                color: RetroTheme.mutedForeground
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Text {
                                            id: isSelectedBadge
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: isSelected
                                            text: "✓"
                                            font.bold: true
                                            font.pixelSize: 13
                                            color: RetroTheme.primary
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

            // === Озвучка выбранного источника ===
            Column {
                id: translationsColumn
                width: parent.width
                spacing: 8
                readonly property var activeTranslations: {
                    if (root.sourceType === "kodik") return root.kodikTranslations
                    if (root.sourceType === "animetka") return root.animetkaTranslations
                    if (root.sourceType === "hentasis") return root.hentasisTranslations
                    if (root.sourceType === "anistar") return root.anistarTranslations
                    if (root.sourceType === "smash")
                        return root.isKodikTranslationId(root.translationId) ? root.kodikTranslations : root.cvhTranslations
                    return root.cvhTranslations
                }
                visible: (root.sourceType === "cvh" || root.sourceType === "kodik" || root.sourceType === "animetka"
                    || root.sourceType === "hentasis"
                    || root.sourceType === "anistar" || root.sourceType === "smash") && activeTranslations.length > 0

                Text {
                    text: "TRANSLATION"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 9
                    color: RetroTheme.mutedForeground
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: translationsColumn.activeTranslations
                        delegate: SourceChip {
                            label: (modelData.title || modelData.name || "?").toUpperCase()
                            active: root.translationId === modelData.id
                            onClicked: {
                                root.translationId = modelData.id
                                root.syncQualityForTranslation()
                            }
                        }
                    }
                }
                Text {
                    text: "QUALITY"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 9
                    color: RetroTheme.mutedForeground
                    visible: root.sourceType === "animetka" && root.currentAnimetkaQualities.length > 0
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    visible: root.sourceType === "animetka" && root.currentAnimetkaQualities.length > 0
                    Repeater {
                        model: root.currentAnimetkaQualities
                        delegate: SourceChip {
                            label: modelData + "P"
                            active: root.streamQuality === modelData
                            onClicked: root.streamQuality = modelData
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: epColumn.height
                color: RetroTheme.card
                border.width: 1
                border.color: RetroTheme.border

                Column {
                    id: epColumn
                    width: parent.width

                    Text {
                        x: 12; topPadding: 8; bottomPadding: 8
                        text: "// episode selector"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 9
                        color: RetroTheme.mutedForeground
                    }
                    Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

                    Row {
                        width: epColumn.width
                        height: 56

                        Rectangle {
                            width: 90; height: parent.height
                            color: prevMouse.containsMouse && root.episode > 1 ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                            opacity: root.episode > 1 ? 1 : 0.3
                            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: RetroTheme.border }
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Text { text: "‹"; font.pixelSize: 14; color: prevMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                                Text { text: "PREV"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: prevMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                            }
                            MouseArea {
                                id: prevMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.episode > 1
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setEp(root.episode - 1)
                            }
                        }

                        Row {
                            width: epColumn.width - 180
                            height: parent.height
                            spacing: 12
                            Item { width: (parent.width - epBox.width - epSuffix.width - 24) / 2; height: 1 }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "EP"; font.family: RetroTheme.fontFamily; font.pixelSize: 11; color: RetroTheme.mutedForeground }
                            Rectangle {
                                id: epBox
                                anchors.verticalCenter: parent.verticalCenter
                                width: 64; height: 34
                                color: RetroTheme.background
                                border.width: 1
                                border.color: epSpin.activeFocus ? RetroTheme.primary : RetroTheme.border
                                TextInput {
                                    id: epSpin
                                    anchors.fill: parent
                                    horizontalAlignment: TextInput.AlignHCenter
                                    verticalAlignment: TextInput.AlignVCenter
                                    text: root.episode
                                    validator: IntValidator { bottom: 1; top: root.maxEpisode }
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 15
                                    color: RetroTheme.primary
                                    selectByMouse: true
                                    onEditingFinished: root.setEp(parseInt(text) || 1)
                                }
                            }
                            Text {
                                id: epSuffix
                                anchors.verticalCenter: parent.verticalCenter
                                text: "/ " + (root.knownEpisodeCount < 9999
                                    ? ((root.knownEpisodeCount < 10 ? "0" : "") + root.knownEpisodeCount)
                                    : "??")
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 11
                                color: RetroTheme.mutedForeground
                            }
                        }

                        Rectangle {
                            width: 90; height: parent.height
                            color: nextMouse.containsMouse && root.episode < root.maxEpisode ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                            opacity: root.episode < root.maxEpisode ? 1 : 0.3
                            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: RetroTheme.border }
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Text { text: "NEXT"; font.family: RetroTheme.fontFamily; font.pixelSize: 10; color: nextMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                                Text { text: "›"; font.pixelSize: 14; color: nextMouse.containsMouse ? RetroTheme.primary : RetroTheme.mutedForeground }
                            }
                            MouseArea {
                                id: nextMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.episode < root.maxEpisode
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setEp(root.episode + 1)
                            }
                        }
                    }
                }
            }
        }

        // === RELATED TITLES ===
        Column {
            x: 20
            width: parent.width - 40
            topPadding: 24
            bottomPadding: 24
            spacing: 10
            visible: root.relatedModel.length > 0

            Row {
                width: parent.width
                spacing: 8
                bottomPadding: 8
                Rectangle { width: 3; height: 12; color: RetroTheme.accent; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: "RELATED TITLES"
                    font.family: RetroTheme.fontFamily
                    font.bold: true
                    font.pixelSize: 12
                    color: RetroTheme.foreground
                }
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border }

            ListView {
                id: relatedList
                width: parent.width
                height: 168 * 1.5 + 46
                orientation: ListView.Horizontal
                spacing: 12
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.relatedModel
                delegate: RetroCard {
                    anime: modelData
                    onClicked: root.openAnime(modelData)
                }
            }
        }
    }
    }

    // Файловый пикер — раздача с несколькими файлами, TorrServer не смог
    // выбрать сам (порт того же блока из DetailView.qml).
    Rectangle {
        id: torrentFilePicker
        property var files: []
        visible: false
        anchors.centerIn: parent
        z: 400
        width: Math.min(parent.width * 0.85, 620)
        height: Math.min(pickerColumn.implicitHeight + 24, parent.height * 0.7)
        color: RetroTheme.card
        border.width: 1
        border.color: RetroTheme.primary
        clip: true

        Column {
            id: pickerColumn
            width: parent.width
            padding: 16
            spacing: 10

            Text {
                text: "// выбери файл для воспроизведения"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 12
                color: RetroTheme.primary
            }

            Flickable {
                width: parent.width - 32
                height: Math.min(torrentFilePicker.files.length * 52, torrentFilePicker.height - 90)
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
                            height: 44
                            color: fileHover.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : RetroTheme.background
                            border.width: 1
                            border.color: fileHover.containsMouse ? RetroTheme.primary : RetroTheme.border

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 10
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: modelData.name
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 11
                                    color: RetroTheme.foreground
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    text: modelData.sizeMb
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 9
                                    color: RetroTheme.mutedForeground
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

            Rectangle {
                width: cancelText.width + 24
                height: 30
                color: cancelMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                border.width: 1
                border.color: RetroTheme.border
                Text {
                    id: cancelText
                    anchors.centerIn: parent
                    text: "CANCEL"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.foreground
                }
                MouseArea {
                    id: cancelMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: torrentFilePicker.visible = false
                }
            }
        }
    }

    // Полноэкранный слой — "чёрный экран" на всё окно, в который
    // репарентится playerHost при cinemaMode (см. комментарий выше и в
    // RetroShell.qml, где по этому же флагу прячутся тайтлбар/сайдбар).
    Rectangle {
        id: cinemaLayer
        anchors.fill: parent
        visible: root.cinemaMode
        z: 500
        color: RetroTheme.background
        clip: true

        focus: visible
        Keys.onEscapePressed: function(event) {
            root.cinemaMode = false
            event.accepted = true
        }
    }

    // === ПЛЕЕР — порт функционала PlayerOverlay.qml: play/pause, перемотка
    // ±10с, таймлайн с кликом, дорожки звука/субтитров, громкость,
    // полноэкранный режим, авто-скрытие панели при бездействии мыши и
    // весь набор клавиатурных/мышиных команд оригинала (см. Shortcut-ы и
    // MouseArea на видео ниже). Видео всегда заполняет playerHost на 100%.
    Item {
        id: playerHost
        parent: root.cinemaMode ? cinemaLayer : inlinePlayerSlot
        anchors.fill: parent
        clip: true

        readonly property bool playerActive: player.hasMedia || player.loading || playback.buffering

        // Авто-скрытие панели (как bottomChrome в PlayerOverlay.qml, но без
        // отдельных "зон" — тут один общий таймер на всю панель+таймлайн).
        property bool controlsVisible: true
        property bool uiPinned: false

        function showControls() {
            playerHost.controlsVisible = true
            if (!playerHost.uiPinned)
                hideControlsTimer.restart()
        }

        Timer {
            id: hideControlsTimer
            interval: 2500
            onTriggered: {
                if (!playerHost.uiPinned)
                    playerHost.controlsVisible = false
            }
        }

        onPlayerActiveChanged: if (playerActive) playerHost.showControls()

        // === Клавиатура — как в PlayerOverlay.qml обычной темы ===
        Shortcut {
            sequence: "Space"
            enabled: playerHost.playerActive
            onActivated: {
                player.paused = !player.paused
                playerHost.showControls()
            }
        }
        Shortcut {
            sequence: "F"
            enabled: playerHost.playerActive
            onActivated: {
                root.cinemaMode = !root.cinemaMode
                playerHost.showControls()
            }
        }
        Shortcut {
            sequence: "Left"
            enabled: playerHost.playerActive
            onActivated: {
                player.seekRelative(-10)
                playerHost.showControls()
            }
        }
        Shortcut {
            sequence: "Right"
            enabled: playerHost.playerActive
            onActivated: {
                player.seekRelative(10)
                playerHost.showControls()
            }
        }
        Shortcut {
            sequence: "M"
            enabled: playerHost.playerActive
            onActivated: {
                player.muted = !player.muted
                playerHost.showControls()
            }
        }
        Shortcut {
            sequence: "Up"
            enabled: playerHost.playerActive
            onActivated: {
                player.volume = Math.min(100, player.volume + 5)
                playerHost.showControls()
            }
        }
        Shortcut {
            sequence: "Down"
            enabled: playerHost.playerActive
            onActivated: {
                player.volume = Math.max(0, player.volume - 5)
                playerHost.showControls()
            }
        }

        function fmtTime(seconds) {
            if (!seconds || seconds < 0 || isNaN(seconds))
                return "0:00"
            const s = Math.floor(seconds)
            const h = Math.floor(s / 3600)
            const m = Math.floor((s % 3600) / 60)
            const sec = s % 60
            const mm = String(m).padStart(2, "0")
            const ss = String(sec).padStart(2, "0")
            return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
        }

        MpvPlayer {
            id: player
            anchors.fill: parent
            Component.onCompleted: playback.attachPlayer(player)
            Component.onDestruction: {
                root.cinemaMode = false
                player.stop()
            }

            // Мышь на видео — весь набор команд из PlayerOverlay.qml:
            // ЛКМ — пауза, ПКМ — закрепить панель (не прячется), двойной
            // клик — полноэкранный режим, колесо — громкость (±5),
            // Shift+колесо — перемотка (±10с). Движение мыши показывает
            // панель и сбрасывает таймер авто-скрытия; курсор прячется
            // вместе с панелью.
            Timer {
                id: retroClickTimer
                // Как в PlayerOverlay.qml (~250 мс mpv/uosc): одиночный клик
                // подтверждается, только если за это время не пришёл второй.
                interval: 250
                onTriggered: player.paused = !player.paused
            }
            MouseArea {
                id: playerMouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: playerHost.controlsVisible ? Qt.ArrowCursor : Qt.BlankCursor
                // Для детекции двойного клика по mpv-правилам (время + расстояние).
                property point lastClickPos: Qt.point(-10000, -10000)
                property real lastClickTime: 0
                onPositionChanged: playerHost.showControls()
                onEntered: playerHost.showControls()

                onClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton) {
                        lastClickPos = Qt.point(mouse.x, mouse.y)
                        lastClickTime = Date.now()
                        retroClickTimer.restart()
                    } else if (mouse.button === Qt.RightButton) {
                        playerHost.uiPinned = !playerHost.uiPinned
                        playerHost.showControls()
                    }
                }
                onDoubleClicked: function(mouse) {
                    if (mouse.button !== Qt.LeftButton)
                        return
                    retroClickTimer.stop()
                    // Qt считает двойным кликом клики в пределах ~400 мс — из-за
                    // этого быстрые pause/play уходили в fullscreen (а в retro —
                    // ещё и успевали переключить паузу). Ужесточаем как mpv/uosc:
                    // двойной клик только если клики быстрые (<=250 мс) и рядом.
                    var elapsed = Date.now() - lastClickTime
                    var dist = Math.hypot(mouse.x - lastClickPos.x, mouse.y - lastClickPos.y)
                    if (elapsed > 250 || dist > 24) {
                        player.paused = !player.paused
                        return
                    }
                    root.cinemaMode = !root.cinemaMode
                }
                onWheel: function(wheel) {
                    // Вне cinemaMode колесо должно скроллить страницу
                    // (detailFlick), а не крутить громкость — иначе просто
                    // невозможно прокрутить карточку колесом, если курсор
                    // над встроенным плеером.
                    if (!root.cinemaMode) {
                        wheel.accepted = false
                        return
                    }
                    playerHost.showControls()
                    if (wheel.modifiers & Qt.ShiftModifier) {
                        player.seekRelative(wheel.angleDelta.y > 0 ? 10 : -10)
                    } else {
                        player.volume = Math.max(0, Math.min(100, player.volume + (wheel.angleDelta.y > 0 ? 5 : -5)))
                    }
                }
            }
        }

        // Таймлайн — кликабельный, перемотка по позиции клика. Прячется
        // вместе с controlsBar (см. controlsOverlay ниже), но наведение на
        // него само по себе тоже держит панель показанной.
        Rectangle {
            id: timeline
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: controlsBar.top
            height: 8
            opacity: playerHost.controlsVisible ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 200 } }
            color: Qt.rgba(1, 1, 1, 0.12)

            HoverHandler { onHoveredChanged: if (hovered) playerHost.showControls() }

            Rectangle {
                height: parent.height
                width: player.duration > 0 ? parent.width * (player.position / player.duration) : 0
                color: RetroTheme.primary
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    if (player.duration > 0)
                        player.seek((mouse.x / timeline.width) * player.duration)
                }
            }
        }

        Rectangle {
            id: controlsBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 52
            opacity: playerHost.controlsVisible ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 200 } }
            color: RetroTheme.card
            border.width: 1
            border.color: RetroTheme.border

            HoverHandler { onHoveredChanged: if (hovered) playerHost.showControls() }

                component IconBtn: Item {
                    id: iconBtn
                    property string icon: ""
                    // enabled уже есть у Item — переиспользуем встроенное
                    // свойство вместо своего (иначе Qt ругается, что оно
                    // перекрывает базовое).
                    signal clicked()
                    width: 34; height: 34
                    opacity: iconBtn.enabled ? (mouse.containsMouse ? 1 : 0.85) : 0.3
                    scale: mouse.containsMouse && iconBtn.enabled ? 1.06 : 1.0
                    Behavior on scale { NumberAnimation { duration: 90 } }

                    Image {
                        anchors.fill: parent
                        source: iconBtn.icon
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: iconBtn.enabled
                        cursorShape: Qt.PointingHandCursor
                        onClicked: iconBtn.clicked()
                    }
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/rewind-10.svg")
                        onClicked: player.seekRelative(-10)
                    }
                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/" + (player.paused ? "play.svg" : "pause.svg"))
                        onClicked: player.paused = !player.paused
                    }
                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/forward-10.svg")
                        onClicked: player.seekRelative(10)
                    }
                    // Пропуск опенинга — 1:27 вперёд.
                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/skip-opening.svg")
                        onClicked: player.seekRelative(87)
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 8
                        text: playerHost.fmtTime(player.position) + " / " + playerHost.fmtTime(player.duration)
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 10
                        color: RetroTheme.mutedForeground
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/episode-prev.svg")
                        enabled: root.episode > 1
                        onClicked: root.playEpisode(root.episode - 1)
                    }
                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/episode-next.svg")
                        enabled: root.episode < root.maxEpisode
                        onClicked: root.playEpisode(root.episode + 1)
                    }
                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/audio.svg")
                        enabled: player.audioTracks.length > 1
                        onClicked: trackPicker.mode = (trackPicker.mode === "audio" ? "" : "audio")
                    }
                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/subtitles.svg")
                        enabled: player.subtitleTracks.length > 0
                        onClicked: trackPicker.mode = (trackPicker.mode === "subtitle" ? "" : "subtitle")
                    }
                    IconBtn {
                        id: audioSyncBtn
                        icon: Qt.resolvedUrl("assets/player-retro/audio-sync.svg")
                        // Ручная синхронизация звука с видео нужна в первую
                        // очередь в смэше (звук и видео — из разных источников,
                        // у них разной длины опенинги/интро), но полезна и
                        // в целом при рассинхроне любого потока.
                        onClicked: audioSyncPanel.visible = !audioSyncPanel.visible
                    }

                    Item {
                        id: volumeBox
                        width: 90; height: 34
                        anchors.verticalCenter: parent.verticalCenter

                        Row {
                            anchors.fill: parent
                            spacing: 6
                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 30
                                source: Qt.resolvedUrl("assets/player-retro/volume.svg")
                                fillMode: Image.PreserveAspectFit
                            }
                            Rectangle {
                                id: volTrack
                                anchors.verticalCenter: parent.verticalCenter
                                width: 44; height: 6
                                color: Qt.rgba(1, 1, 1, 0.12)
                                Rectangle {
                                    height: parent.height
                                    width: parent.width * (player.volume / 100)
                                    color: RetroTheme.primary
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -6
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: function(mouse) { player.volume = Math.max(0, Math.min(100, Math.round((mouse.x / volTrack.width) * 100))) }
                                    onPositionChanged: function(mouse) {
                                        if (pressed)
                                            player.volume = Math.max(0, Math.min(100, Math.round((mouse.x / volTrack.width) * 100)))
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: speedBox
                        width: 44; height: 26
                        anchors.verticalCenter: parent.verticalCenter
                        color: RetroTheme.background
                        border.width: 1
                        border.color: speedInput.activeFocus ? RetroTheme.primary : RetroTheme.border

                        TextInput {
                            id: speedInput
                            anchors.fill: parent
                            horizontalAlignment: TextInput.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                            text: player.speed.toFixed(2) + "x"
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,1}[.,]?[0-9]{0,2}x?/ }
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 11
                            color: RetroTheme.primary
                            selectByMouse: true
                            onEditingFinished: {
                                const parsed = parseFloat(text.replace(",", ".").replace("x", ""))
                                if (!isNaN(parsed))
                                    player.speed = Math.max(0.25, Math.min(4.0, parsed))
                                text = player.speed.toFixed(2) + "x"
                            }
                            Connections {
                                target: player
                                function onSpeedChanged() { speedInput.text = player.speed.toFixed(2) + "x" }
                            }
                        }
                    }

                    IconBtn {
                        icon: Qt.resolvedUrl("assets/player-retro/" + (root.cinemaMode ? "fullscreen-exit.svg" : "fullscreen.svg"))
                        onClicked: root.cinemaMode = !root.cinemaMode
                    }
                }

                // Ручной сдвиг звука относительно видео — нужен в смэше, где
                // видео (торрент) и звук (Kodik/CVH) из разных релизов и могут
                // не совпадать по длине опенинга/интро на несколько секунд.
                Rectangle {
                    id: audioSyncPanel
                    visible: false
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.bottom: parent.top
                    anchors.bottomMargin: 8
                    width: 230
                    height: syncColumn.implicitHeight + 20
                    color: RetroTheme.card
                    border.width: 1
                    border.color: RetroTheme.primary
                    z: 50

                    Column {
                        id: syncColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Text {
                            text: "// audio sync offset"
                            font.family: RetroTheme.fontFamily
                            font.pixelSize: 9
                            color: RetroTheme.mutedForeground
                        }
                        Row {
                            width: parent.width
                            spacing: 8

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 64; height: 26
                                color: RetroTheme.background
                                border.width: 1
                                border.color: delayInput.activeFocus ? RetroTheme.primary : RetroTheme.border
                                // Значение вбивается напрямую (в секундах, "-6.6" и
                                // т.п.) — плюс/минус кнопки по 0.1с остаются рядом
                                // для мелкой подстройки, вбивать десятки кликов
                                // ими для большого сдвига не нужно.
                                TextInput {
                                    id: delayInput
                                    anchors.fill: parent
                                    horizontalAlignment: TextInput.AlignHCenter
                                    verticalAlignment: TextInput.AlignVCenter
                                    text: (player.audioDelay >= 0 ? "+" : "") + player.audioDelay.toFixed(1) + "s"
                                    validator: RegularExpressionValidator { regularExpression: /-?\d*\.?\d*s?/ }
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 13
                                    color: RetroTheme.primary
                                    selectByMouse: true
                                    onEditingFinished: {
                                        var v = parseFloat(text.replace("s", "").replace(",", "."))
                                        player.audioDelay = isNaN(v) ? 0 : Math.round(v * 10) / 10
                                    }
                                }
                            }
                            Rectangle {
                                width: 30; height: 26
                                color: minusMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                                border.width: 1
                                border.color: RetroTheme.border
                                Text { anchors.centerIn: parent; text: "-"; font.pixelSize: 16; color: RetroTheme.foreground }
                                MouseArea {
                                    id: minusMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: player.audioDelay = Math.round((player.audioDelay - 0.1) * 10) / 10
                                }
                            }
                            Rectangle {
                                width: 30; height: 26
                                color: plusMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                                border.width: 1
                                border.color: RetroTheme.border
                                Text { anchors.centerIn: parent; text: "+"; font.pixelSize: 16; color: RetroTheme.foreground }
                                MouseArea {
                                    id: plusMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: player.audioDelay = Math.round((player.audioDelay + 0.1) * 10) / 10
                                }
                            }
                            Rectangle {
                                width: 60; height: 26
                                color: resetMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                                border.width: 1
                                border.color: RetroTheme.border
                                Text { anchors.centerIn: parent; text: "RESET"; font.family: RetroTheme.fontFamily; font.pixelSize: 8; color: RetroTheme.mutedForeground }
                                MouseArea {
                                    id: resetMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: player.audioDelay = 0
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 8

                            Rectangle {
                                width: parent.width
                                height: 26
                                color: saveMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                                border.width: 1
                                border.color: RetroTheme.primary
                                Text {
                                    anchors.centerIn: parent
                                    text: saveConfirmTimer.running ? "SAVED ✓" : "SAVE PRESET"
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 9
                                    color: RetroTheme.primary
                                }
                                MouseArea {
                                    id: saveMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.item && root.item.id) {
                                            appConfig.setAudioSyncOffset(root.item.id, player.audioDelay)
                                            saveConfirmTimer.restart()
                                        }
                                    }
                                }
                            }
                        }
                        Timer { id: saveConfirmTimer; interval: 1200 }
                    }
                }
            }
        }

        // Выбор озвучки/субтитров — центрированное окно со списком, а не
        // циклический перебор кликами (в некоторых раздачах по 15-20 дорожек,
        // долистывать до нужной по одной было болью).
        Rectangle {
            id: trackPicker
            property string mode: ""   // "audio" | "subtitle" | ""
            readonly property var tracks: mode === "audio" ? player.audioTracks
                : (mode === "subtitle" ? player.subtitleTracks : [])
            visible: mode !== ""
            anchors.centerIn: parent
            z: 60
            width: Math.min(parent.width * 0.7, 420)
            height: Math.min(trackPickerColumn.implicitHeight + 24, parent.height * 0.75)
            color: RetroTheme.card
            border.width: 1
            border.color: RetroTheme.primary
            clip: true

            MouseArea { anchors.fill: parent } // не закрывать по клику мимо списка/кнопок

            Column {
                id: trackPickerColumn
                width: parent.width
                padding: 16
                spacing: 10

                Text {
                    text: trackPicker.mode === "audio" ? "// выбери озвучку" : "// выбери субтитры"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 12
                    color: RetroTheme.primary
                }

                Flickable {
                    width: parent.width - 32
                    height: Math.min(trackListCol.implicitHeight, trackPicker.height - 100)
                    contentWidth: width
                    contentHeight: trackListCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: trackListCol
                        width: parent.width
                        spacing: 4

                        // "Выкл." — только для субтитров, аудио выключить нельзя
                        Rectangle {
                            visible: trackPicker.mode === "subtitle"
                            width: trackListCol.width
                            height: 34
                            color: offHover.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : RetroTheme.background
                            border.width: 1
                            border.color: player.subtitleTracks.findIndex(function(t) { return t.selected }) === -1
                                ? RetroTheme.primary : RetroTheme.border
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Выкл."
                                font.family: RetroTheme.fontFamily
                                font.pixelSize: 11
                                color: RetroTheme.foreground
                            }
                            MouseArea {
                                id: offHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    player.setSubtitleTrack(-1)
                                    trackPicker.mode = ""
                                }
                            }
                        }

                        Repeater {
                            model: trackPicker.tracks
                            delegate: Rectangle {
                                width: trackListCol.width
                                height: 34
                                color: trackHover.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : RetroTheme.background
                                border.width: 1
                                border.color: modelData.selected ? RetroTheme.primary : RetroTheme.border
                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.title || ("Дорожка " + modelData.id)
                                    font.family: RetroTheme.fontFamily
                                    font.pixelSize: 11
                                    color: RetroTheme.foreground
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    id: trackHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (trackPicker.mode === "audio")
                                            player.setAudioTrack(modelData.id)
                                        else
                                            player.setSubtitleTrack(modelData.id)
                                        trackPicker.mode = ""
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: trackPickerCloseText.width + 24
                    height: 30
                    color: trackPickerCloseMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : "transparent"
                    border.width: 1
                    border.color: RetroTheme.border
                    Text {
                        id: trackPickerCloseText
                        anchors.centerIn: parent
                        text: "ЗАКРЫТЬ"
                        font.family: RetroTheme.fontFamily
                        font.pixelSize: 10
                        color: RetroTheme.foreground
                    }
                    MouseArea {
                        id: trackPickerCloseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: trackPicker.mode = ""
                    }
                }
            }
        }
    }
