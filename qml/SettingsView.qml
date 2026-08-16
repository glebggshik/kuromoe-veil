import QtQuick
import QtQuick.Controls
import "components"

Item {
    id: root
    property bool embedded: false
    signal backRequested()

    readonly property int contentWidth: Math.min(width - 64, 560)
    readonly property color fieldText: "white"
    readonly property color fieldPlaceholder: "white"

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.contentWidth
            spacing: 20
            topPadding: 24

            Row {
                width: parent.width
                spacing: 12
                visible: !root.embedded
                Button {
                    text: "← Назад"
                    implicitHeight: 34
                    background: Rectangle { radius: Theme.cornerPill; color: Theme.bgPill }
                    contentItem: Text { text: "← Назад"; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: root.backRequested()
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Настройки"
                    color: Theme.textPrimary
                    font.pixelSize: 22
                    font.bold: true
                }
            }

            Text {
                width: parent.width
                visible: root.embedded
                text: "Настройки"
                color: Theme.textPrimary
                font.pixelSize: 22
                font.bold: true
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 12
                text: "Для торрентов нужен TorrServer. Прокси — для AnimeGO (CVH) и JacRed; потоки CVH и AniLibria обычно работают без него."
            }

            // === Тема оформления ===
            Column {
                id: themeSection
                width: parent.width
                spacing: 8

                // Тема, с которой запущен ТЕКУЩИЙ процесс (main.cpp читает
                // appConfig.theme() только один раз при старте) — снимок на
                // момент открытия настроек. Если выбор в ComboBox уйдёт от
                // этого снимка, значит новое значение уже сохранено в
                // config.ini, но ещё не применено к открытому окну.
                // Без Component.onCompleted это было бы живым биндингом на
                // appConfig.theme (менялось бы синхронно с ним) — нужен
                // именно разовый снимок, поэтому property без ":"-выражения.
                property string startupTheme: ""
                Component.onCompleted: startupTheme = appConfig.theme

                Text { text: "Тема оформления"; color: Theme.textSecondary; font.pixelSize: 12 }
                ComboBox {
                    id: themeBox
                    width: parent.width
                    model: [
                        { value: "dark", label: "Обычная (по умолчанию)" },
                        { value: "retro", label: "Retro Terminal (CRT-фосфор, зелёный)" }
                    ]
                    textRole: "label"
                    currentIndex: appConfig.theme === "retro" ? 1 : 0
                    onActivated: function(index) { appConfig.theme = model[index].value }
                    background: Rectangle { radius: Theme.cornerSmall; color: Theme.bgInput }
                    contentItem: Text {
                        leftPadding: 10
                        text: themeBox.displayText
                        color: root.fieldText
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                Row {
                    spacing: 10
                    visible: appConfig.theme !== themeSection.startupTheme
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Тема применится после перезапуска"
                        color: Theme.warn
                        font.pixelSize: 11
                    }
                    PillButton {
                        text: "Перезапустить сейчас"
                        onClicked: appConfig.restartApplication()
                    }
                }
            }

            // === Рендер плеера ===
            Column {
                width: parent.width
                spacing: 8
                Text { text: "Рендер видео"; color: Theme.textSecondary; font.pixelSize: 12 }
                ComboBox {
                    id: renderModeBox
                    width: parent.width
                    model: [
                        { value: "auto", label: "Авто (GPU → CPU fallback)" },
                        { value: "gpu", label: "GPU (OpenGL)" },
                        { value: "software", label: "Программный (CPU)" }
                    ]
                    textRole: "label"
                    currentIndex: {
                        if (appConfig.playerRenderMode === "gpu") return 1
                        if (appConfig.playerRenderMode === "software") return 2
                        return 0
                    }
                    onActivated: function(index) { appConfig.playerRenderMode = model[index].value }
                    background: Rectangle { radius: Theme.cornerSmall; color: Theme.bgInput }
                    contentItem: Text {
                        leftPadding: 10
                        text: renderModeBox.displayText
                        color: root.fieldText
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                ComboBox {
                    id: fpsLimitBox
                    width: parent.width
                    model: [
                        { value: "auto", label: "FPS: Авто (GPU — без лимита, CPU — 30)" },
                        { value: "unlimited", label: "FPS: Без лимита" },
                        { value: "120", label: "FPS: до 120" },
                        { value: "60", label: "FPS: до 60" },
                        { value: "30", label: "FPS: до 30 (экономия CPU)" }
                    ]
                    textRole: "label"
                    currentIndex: {
                        if (appConfig.playerFpsLimit === "unlimited") return 1
                        if (appConfig.playerFpsLimit === "120") return 2
                        if (appConfig.playerFpsLimit === "60") return 3
                        if (appConfig.playerFpsLimit === "30") return 4
                        return 0
                    }
                    onActivated: function(index) { appConfig.playerFpsLimit = model[index].value }
                    background: Rectangle { radius: Theme.cornerSmall; color: Theme.bgInput }
                    contentItem: Text {
                        leftPadding: 10
                        text: fpsLimitBox.displayText
                        color: root.fieldText
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: "Рендер: перезапуск. FPS лимит — сразу. На GPU+120 Гц мониторе ставь «Без лимита» или «120»."
                }
            }

            // === Прокси ===
            Column {
                width: parent.width
                spacing: 8
                Text { text: "Прокси (AnimeGO / JacRed)"; color: Theme.textSecondary; font.pixelSize: 12 }
                Row {
                    spacing: 10
                    Switch {
                        id: proxySwitch
                        checked: appConfig.proxyEnabled
                        onToggled: appConfig.proxyEnabled = checked
                    }
                    Text {
                        anchors.verticalCenter: proxySwitch.verticalCenter
                        text: "Включить прокси"
                        color: Theme.textPrimary
                        font.pixelSize: 13
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8
                    ComboBox {
                        id: proxyTypeBox
                        width: 110
                        model: ["http", "socks5"]
                        currentIndex: appConfig.proxyType === "socks5" ? 1 : 0
                        onActivated: appConfig.proxyType = currentText
                        background: Rectangle { radius: Theme.cornerSmall; color: Theme.bgInput }
                        contentItem: Text {
                            leftPadding: 10
                            text: proxyTypeBox.displayText
                            color: root.fieldText
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Rectangle {
                        width: parent.width - proxyTypeBox.width - portField.width - 16
                        height: 38
                        radius: Theme.cornerSmall
                        color: Theme.bgInput
                        TextField {
                            id: hostField
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            color: root.fieldText
                            placeholderTextColor: root.fieldPlaceholder
                            selectedTextColor: "#121212"
                            selectionColor: Theme.accent
                            background: Item {}
                            placeholderText: "host"
                            text: appConfig.proxyHost
                            onEditingFinished: appConfig.proxyHost = text
                        }
                    }
                    Rectangle {
                        id: portField
                        width: 72
                        height: 38
                        radius: Theme.cornerSmall
                        color: Theme.bgInput
                        TextField {
                            anchors.fill: parent
                            horizontalAlignment: Text.AlignHCenter
                            color: root.fieldText
                            placeholderTextColor: root.fieldPlaceholder
                            selectedTextColor: "#121212"
                            selectionColor: Theme.accent
                            background: Item {}
                            placeholderText: "port"
                            text: appConfig.proxyPort > 0 ? String(appConfig.proxyPort) : ""
                            inputMethodHints: Qt.ImhDigitsOnly
                            onEditingFinished: appConfig.proxyPort = parseInt(text) || 0
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8
                    Rectangle {
                        width: (parent.width - 8) / 2
                        height: 38
                        radius: Theme.cornerSmall
                        color: Theme.bgInput
                        TextField {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            color: root.fieldText
                            placeholderTextColor: root.fieldPlaceholder
                            selectedTextColor: "#121212"
                            selectionColor: Theme.accent
                            background: Item {}
                            placeholderText: "логин (опц.)"
                            text: appConfig.proxyUser
                            onEditingFinished: appConfig.proxyUser = text.trim()
                        }
                    }
                    Rectangle {
                        width: (parent.width - 8) / 2
                        height: 38
                        radius: Theme.cornerSmall
                        color: Theme.bgInput
                        TextField {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            color: root.fieldText
                            placeholderTextColor: root.fieldPlaceholder
                            selectedTextColor: "#121212"
                            selectionColor: Theme.accent
                            background: Item {}
                            placeholderText: "пароль (опц.)"
                            echoMode: TextInput.Password
                            text: appConfig.proxyPassword
                            onEditingFinished: appConfig.proxyPassword = text.trim()
                        }
                    }
                }
                Text {
                    width: parent.width
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: appConfig.proxyEnabled && appConfig.mpvProxyUrl() !== ""
                        ? ("mpv: " + appConfig.mpvProxyUrl())
                        : "mpv: без прокси"
                }
            }

            // === Kodik sidecar (не используется — CVH через AnimeGO) ===
            Column {
                width: parent.width
                spacing: 6
                visible: false
                Text {
                    text: "Kodik resolver (локальный sidecar, опционально)"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Rectangle {
                    width: parent.width
                    height: 38
                    radius: Theme.cornerSmall
                    color: Theme.bgInput
                    TextField {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        text: appConfig.kodikResolverUrl
                        color: root.fieldText
                        placeholderTextColor: root.fieldPlaceholder
                        selectedTextColor: "#121212"
                        selectionColor: Theme.accent
                        background: Item {}
                        placeholderText: "http://127.0.0.1:8765"
                        onEditingFinished: appConfig.kodikResolverUrl = text.trim()
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: "Если /ftor даёт 500 — запусти tools/kodik_resolver.py и укажи URL. Прокси для sidecar настраивается отдельно в скрипте."
                }
            }

            // === TorrServer ===
            Column {
                width: parent.width
                spacing: 6
                Text { text: "TorrServer (торренты — обязательно для «🧲 Торренты»)"; color: Theme.textSecondary; font.pixelSize: 12 }
                Row {
                    width: parent.width
                    spacing: 8
                    Rectangle {
                        width: parent.width - torrBrowseBtn.width - 8
                        height: 38
                        radius: Theme.cornerSmall
                        color: Theme.bgInput
                        TextField {
                            id: torrserverField
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            text: appConfig.torrServerPath
                            color: root.fieldText
                            placeholderTextColor: root.fieldPlaceholder
                            selectedTextColor: "#121212"
                            selectionColor: Theme.accent
                            background: Item {}
                            placeholderText: "~/Downloads/TorrServer-windows-amd64.exe"
                            onEditingFinished: appConfig.torrServerPath = text
                        }
                    }
                    PillButton {
                        id: torrBrowseBtn
                        text: "Найти"
                        onClicked: {
                            var p = appConfig.autoDetectTorrServer()
                            if (p) {
                                torrserverField.text = p
                                appConfig.torrServerPath = p
                            }
                        }
                    }

                }
            }

            // === JacRed ===
            Column {
                width: parent.width
                spacing: 6
                Text { text: "JacRed URL"; color: Theme.textSecondary; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width
                    height: 38
                    radius: Theme.cornerSmall
                    color: Theme.bgInput
                    TextField {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        text: appConfig.jacredUrl
                        color: root.fieldText
                        placeholderTextColor: root.fieldPlaceholder
                        selectedTextColor: "#121212"
                        selectionColor: Theme.accent
                        background: Item {}
                        placeholderText: "https://jac.red"
                        onEditingFinished: appConfig.jacredUrl = text
                    }
                }
                Text { text: "Kodik token"; color: Theme.textSecondary; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width
                    height: 38
                    radius: Theme.cornerSmall
                    color: Theme.bgInput
                    TextField {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        text: appConfig.kodikToken
                        color: root.fieldText
                        placeholderTextColor: root.fieldPlaceholder
                        selectedTextColor: "#121212"
                        selectionColor: Theme.accent
                        background: Item {}
                        placeholderText: "вшитый публичный токен"
                        onEditingFinished: appConfig.kodikToken = text.trim()
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: "В исходниках есть публичный токен по умолчанию. Пустое поле вернёт его. Свой — если этот перестал работать."
                }
            }

            Row {
                spacing: 10
                Switch {
                    id: chinaSwitch
                    checked: appConfig.excludeChinese
                    onToggled: appConfig.excludeChinese = checked
                }
                Text {
                    anchors.verticalCenter: chinaSwitch.verticalCenter
                    text: "Скрывать китайские аниме"
                    color: Theme.textPrimary
                    font.pixelSize: 13
                }
            }

            // === Кэш ===
            Column {
                width: parent.width
                spacing: 6
                Text { text: "Кэш изображений"; color: Theme.textSecondary; font.pixelSize: 12 }
                Row {
                    width: parent.width
                    spacing: 10
                    PillButton {
                        id: clearCacheBtn
                        text: "Очистить кэш"
                        onClicked: {
                            var n = posterCache.clearDiskCache()
                            cacheClearedLabel.text = "Удалено файлов: " + n + " (перезапустите для применения)"
                        }
                    }
                    Text {
                        id: cacheClearedLabel
                        anchors.verticalCenter: clearCacheBtn.verticalCenter
                        text: ""
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                Text {
                    width: parent.width
                    text: "Удаляет скачанные постеры и баннеры (включая устаревшие/битые). История просмотра и настройки не затрагиваются."
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}