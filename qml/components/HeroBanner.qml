import QtQuick
import AnimeClient 1.0

// Hero: heroImageLocal (file://) или remote через posterCache. Qt Image + PreserveAspectCrop.
Rectangle {
    id: root
    color: Theme.bgCard
    clip: true

    property var heroData: ({})
    property bool imagesEnabled: true
    property int slideIndex: 0
    signal playClicked(var item)

    property string loadedFile: ""

    readonly property var slides: {
        var s = heroData && heroData.heroSlides
        if (s && s.length > 0)
            return s
        return heroData ? [heroData] : []
    }

    readonly property var currentSlide: {
        if (!root.slides || root.slides.length === 0)
            return root.heroData || {}
        var s = root.slides[root.slideIndex]
        return s || root.heroData || {}
    }

    function posterOk(u) {
        return u.length > 0 && u.indexOf("/missing_") < 0
    }

    function posterOnlyUrl(slide) {
        if (!slide || typeof slide !== "object")
            return ""
        var hd = slide.posterHd || ""
        var p = slide.poster || ""
        if (posterOk(hd)) return hd
        if (posterOk(p)) return p
        return ""
    }

    // Хиро — только широкий баннер. Вертикальный постер сюда не годится: в широком
    // прямоугольнике с PreserveAspectCrop портретная картинка превращается в нечитаемый
    // увеличенный обрезок. Нет баннера — пусть будет плейсхолдер-градиент, а не это.
    function remotePosterUrl(slide) {
        if (!slide || typeof slide !== "object")
            return ""
        var banner = slide.heroBanner || ""
        return posterOk(banner) ? banner : ""
    }

    function immediateDisplayUrl(slide) {
        if (!slide || typeof slide !== "object")
            return ""
        var banner = slide.heroBanner || ""
        if (!posterOk(banner))
            return ""
        var cachedBanner = posterCache.cachedFile(banner)
        return cachedBanner.length > 0 ? cachedBanner : banner
    }

    readonly property string heroRemoteSource: root.remotePosterUrl(root.currentSlide)
    readonly property string heroDisplaySource: root.immediateDisplayUrl(root.currentSlide)

    readonly property string heroPlaceholder: {
        var slide = root.currentSlide
        var t = slide && slide.title ? String(slide.title).trim() : "?"
        return t.length > 0 ? t.charAt(0).toUpperCase() : "?"
    }

    function queueHeroCache(slide) {
        if (!slide || typeof slide !== "object")
            return
        var banner = slide.heroBanner || ""
        if (posterOk(banner) && posterCache.isRemotePoster(banner))
            posterCache.requestPriority(banner)
    }

    function refreshHeroImage() {
        loadedFile = ""
        if (!root.imagesEnabled)
            return

        var bannerUrl = root.currentSlide && root.currentSlide.heroBanner
            ? String(root.currentSlide.heroBanner) : ""
        if (posterOk(bannerUrl)) {
            var cachedBanner = posterCache.cachedFile(bannerUrl)
            if (cachedBanner.length > 0) {
                loadedFile = cachedBanner
                queueHeroCache(root.currentSlide)
                return
            }
            if (root.heroData && root.heroData.heroImageLocal) {
                var local = String(root.heroData.heroImageLocal)
                // lastHero мог устареть после «очистить кэш» — только если файл ещё баннер.
                if (local.length > 0 && local === cachedBanner)
                    loadedFile = local
                else if (local.length > 0 && cachedBanner.length === 0)
                    queueHeroCache(root.currentSlide)
                if (loadedFile.length > 0)
                    return
            }
        }

        var display = root.heroDisplaySource
        if (display.length === 0)
            return

        // dist/KuroMoe_Veil: proxy off → QML Image тянет https напрямую. С SOCKS5 — только file://.
        if (display.indexOf("http") === 0 && appConfig.proxyEnabled) {
            queueHeroCache(root.currentSlide)
            return
        }

        loadedFile = display
        queueHeroCache(root.currentSlide)
    }

    function setData(item) {
        root.slideIndex = 0
        root.heroData = item || {}
        refreshHeroImage()
    }

    Connections {
        target: posterCache
        function onPosterReady(remoteUrl, fileUrl) {
            var slide = root.currentSlide
            if (!slide)
                return
            var banner = slide.heroBanner || ""
            // Только баннер — вертикальный постер из preloadCatalog сюда не подставляем.
            if (remoteUrl === banner && root.posterOk(banner))
                root.loadedFile = fileUrl
        }
    }

    onImagesEnabledChanged: refreshHeroImage()
    onHeroDisplaySourceChanged: refreshHeroImage()
    onSlideIndexChanged: refreshHeroImage()

    Timer {
        id: rotateTimer
        interval: 9000
        running: root.imagesEnabled && root.slides.length > 1 && !(root.heroData && root.heroData.continuing)
        repeat: true
        onTriggered: root.slideIndex = (root.slideIndex + 1) % root.slides.length
    }

    Rectangle {
        anchors.fill: parent
        visible: heroPoster.status !== Image.Ready || root.loadedFile.length === 0
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2a2a3d" }
            GradientStop { position: 1.0; color: "#1a1a28" }
        }
        Text {
            anchors.centerIn: parent
            text: root.heroPlaceholder
            color: "#55ffffff"
            font.pixelSize: 72
            font.bold: true
        }
    }

    Image {
        id: heroPoster
        anchors.fill: parent
        source: root.loadedFile
        // Фиксированный щедрый bounding-box, НЕ зависящий от размеров виджета.
        // Важно: с PreserveAspectCrop в широкой, но низкой плашке (≈1280×280)
        // картинка масштабируется по ШИРИНЕ, и для высокого баннера (напр.
        // FMA 1900×1188, соотношение 1.6) нужно ~1600px высоты декодирования.
        // Прежний вариант капал высоту (~560) → высокий баннер декодировался в
        // 896×560 и потом РАСТЯГИВАЛСЯ на всю ширину плашки = размытие ("плохой
        // постер" при старте). Бокс 2560×1600 вмещает и широкие тонкие баннеры
        // (1900×400 → нативно), и высокие (1900×1188 → нативно) на полном
        // разрешении. От транзиентной геометрии в первый кадр тоже не зависит.
        // 1920×1080 хватает для hero; 2560×1600 жрало ~16 МБ RGBA на баннер.
        sourceSize: Qt.size(1920, 1080)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        cache: false
        opacity: status === Image.Ready ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 450; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.55; color: "#12000000" }
            GradientStop { position: 1.0; color: "#77000000" }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: parent.width * 0.05
        anchors.bottomMargin: parent.height * 0.12
        width: parent.width * 0.55
        spacing: 8

        Text {
            width: parent.width
            text: (root.heroData && root.heroData.continuing ? "Продолжить: " : "")
                + (root.currentSlide && root.currentSlide.title ? root.currentSlide.title : "")
            color: "white"
            font.pixelSize: 26
            font.bold: true
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: (root.heroData && root.heroData.continuing && root.heroData.lastEpisode
                ? "Серия " + root.heroData.lastEpisode + " · "
                : "")
                + (root.currentSlide && root.currentSlide.description ? root.currentSlide.description : "")
            color: "#d1d1d6"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        Row {
            visible: root.slides.length > 1 && !(root.heroData && root.heroData.continuing)
            spacing: 6
            Repeater {
                model: root.slides.length
                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: index === root.slideIndex ? Theme.accentLight : "#55ffffff"
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (root.currentSlide)
                root.playClicked(root.currentSlide)
        }
    }

    opacity: 0
    Component.onCompleted: fadeIn.start()
    NumberAnimation { id: fadeIn; target: root; property: "opacity"; to: 1; duration: 300; easing.type: Easing.OutCubic }
}