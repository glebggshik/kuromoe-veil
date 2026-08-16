import QtQuick
import AnimeClient 1.0

// Порт SettingsView.qml (реальный экран настроек) на ретро-скин — раньше
// тут были только фейковые локальные тумблеры (CRT_SCANLINES и т.п.),
// ничего не сохранявшие. Теперь все поля читают/пишут те же appConfig-
// свойства, что и обычная тема: тема оформления, рендер видео, прокси,
// TorrServer, JacRed, фильтр Китая, очистка кэша постеров.
Flickable {
    id: root
    contentWidth: width
    contentHeight: contentColumn.height
    clip: true

    // Снимок темы на момент открытия — как в обычной SettingsView.qml,
    // чтобы корректно показать "нужен перезапуск" (appConfig.theme меняется
    // сразу в config.ini, а к текущему окну применится только после рестарта).
    property string startupTheme: ""
    Component.onCompleted: root.startupTheme = appConfig.theme

    // === Переиспользуемые терминальные контролы ===
    component FieldLabel: Text {
        font.family: RetroTheme.fontFamily
        font.pixelSize: 9
        color: RetroTheme.mutedForeground
    }

    component RetroTextField: Rectangle {
        id: fieldRoot
        property alias text: input.text
        property string placeholder: ""
        property bool password: false
        signal edited(string value)

        height: 34
        color: RetroTheme.background
        border.width: 1
        border.color: input.activeFocus ? RetroTheme.primary : RetroTheme.border

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            color: RetroTheme.foreground
            selectByMouse: true
            echoMode: fieldRoot.password ? TextInput.Password : TextInput.Normal
            onEditingFinished: fieldRoot.edited(text)

            Text {
                visible: input.text.length === 0 && !input.activeFocus
                text: fieldRoot.placeholder
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
        }
    }

    component RetroButton: Rectangle {
        id: btnRoot
        property string label: ""
        signal clicked()

        width: btnText.width + 24
        height: 32
        color: btnMouse.containsMouse ? Qt.rgba(0.220, 0.973, 0.573, 0.12) : RetroTheme.card
        border.width: 1
        border.color: btnMouse.containsMouse ? RetroTheme.primary : RetroTheme.border

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btnRoot.label
            font.family: RetroTheme.fontFamily
            font.pixelSize: 11
            color: btnMouse.containsMouse ? RetroTheme.primary : RetroTheme.foreground
        }
        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btnRoot.clicked()
        }
    }

    component RetroSwitch: Rectangle {
        id: swRoot
        property bool on: false
        signal toggled(bool value)

        width: 56; height: 24
        color: on ? Qt.rgba(0.220, 0.973, 0.573, 0.2) : RetroTheme.muted
        border.width: 1
        border.color: on ? RetroTheme.primary : RetroTheme.border

        Rectangle {
            x: swRoot.on ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            width: 24; height: 16
            color: swRoot.on ? RetroTheme.primary : Qt.rgba(0.435, 0.506, 0.447, 0.4)
            Behavior on x { NumberAnimation { duration: 120 } }
            Text {
                anchors.centerIn: parent
                text: swRoot.on ? "ON" : "OFF"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 7
                color: swRoot.on ? RetroTheme.primaryForeground : RetroTheme.background
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: swRoot.toggled(!swRoot.on)
        }
    }

    Column {
        id: contentColumn
        width: root.width
        topPadding: 20
        bottomPadding: 24
        spacing: 22

        Column {
            x: 20
            width: parent.width - 40
            spacing: 4
            bottomPadding: 4

            Text {
                text: "SETTINGS"
                font.family: RetroTheme.fontFamily
                font.bold: true
                font.pixelSize: 20
                color: RetroTheme.primary
            }
            Text {
                text: "// ~/.config/terminal.watch/prefs.cfg"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.mutedForeground
            }
            Text {
                width: parent.width
                text: "// Для торрентов нужен TorrServer. Прокси — для AnimeGO (CVH) и JacRed; потоки CVH и AniLibria обычно работают без него."
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.mutedForeground
                wrapMode: Text.WordWrap
            }
            Rectangle { width: parent.width; height: 1; color: RetroTheme.border; anchors.topMargin: 4 }
        }

        // === ТЕМА ОФОРМЛЕНИЯ ===
        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 8

            FieldLabel { text: "THEME" }
            RetroSelect {
                width: parent.width
                label: "Тема оформления"
                // Retro — тема по умолчанию и первая в списке; классическая
                // переименована в «Старая тема».
                options: ["Retro Terminal (CRT-фосфор, зелёный) — по умолчанию", "Старая тема"]
                Component.onCompleted: value = appConfig.theme === "retro" ? options[0] : options[1]
                onChanged: function(v) { appConfig.theme = (v === options[0]) ? "retro" : "dark" }
            }
            Row {
                spacing: 10
                visible: appConfig.theme !== root.startupTheme
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "// тема применится после перезапуска"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.accent
                }
                RetroButton { label: "RESTART NOW"; onClicked: appConfig.restartApplication() }
            }
        }

        // === РЕНДЕР ВИДЕО ===
        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 8

            FieldLabel { text: "PLAYBACK" }
            RetroSelect {
                width: parent.width
                label: "Рендер видео"
                options: ["Авто (GPU → CPU fallback)", "GPU (OpenGL)", "Программный (CPU)"]
                Component.onCompleted: {
                    value = appConfig.playerRenderMode === "gpu" ? options[1]
                        : appConfig.playerRenderMode === "software" ? options[2]
                        : options[0]
                }
                onChanged: function(v) {
                    appConfig.playerRenderMode = v === options[1] ? "gpu" : v === options[2] ? "software" : "auto"
                }
            }
            RetroSelect {
                width: parent.width
                label: "FPS лимит"
                options: [
                    "Авто (GPU — без лимита, CPU — 30)",
                    "Без лимита", "До 120", "До 60", "До 30 (экономия CPU)"
                ]
                Component.onCompleted: {
                    value = appConfig.playerFpsLimit === "unlimited" ? options[1]
                        : appConfig.playerFpsLimit === "120" ? options[2]
                        : appConfig.playerFpsLimit === "60" ? options[3]
                        : appConfig.playerFpsLimit === "30" ? options[4]
                        : options[0]
                }
                onChanged: function(v) {
                    appConfig.playerFpsLimit = v === options[1] ? "unlimited"
                        : v === options[2] ? "120"
                        : v === options[3] ? "60"
                        : v === options[4] ? "30"
                        : "auto"
                }
            }
            Text {
                width: parent.width
                text: "// рендер — после перезапуска, FPS лимит — сразу. На GPU+120Гц ставь \"без лимита\" или \"до 120\"."
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.mutedForeground
                wrapMode: Text.WordWrap
            }
        }

        // === ПРОКСИ ===
        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 10

            FieldLabel { text: "PROXY (AnimeGO / JacRed)" }
            Row {
                spacing: 10
                RetroSwitch { on: appConfig.proxyEnabled; onToggled: function(v) { appConfig.proxyEnabled = v } }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Включить прокси"
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 11
                    color: RetroTheme.foreground
                }
            }
            Row {
                width: parent.width
                spacing: 8
                RetroSelect {
                    width: 130
                    label: "Тип"
                    options: ["http", "socks5"]
                    Component.onCompleted: value = appConfig.proxyType === "socks5" ? "socks5" : "http"
                    onChanged: function(v) { appConfig.proxyType = v }
                }
                Column {
                    width: parent.width - 130 - 8 - 90 - 8
                    spacing: 4
                    FieldLabel { text: "HOST" }
                    RetroTextField {
                        width: parent.width
                        placeholder: "host"
                        text: appConfig.proxyHost
                        onEdited: function(v) { appConfig.proxyHost = v }
                    }
                }
                Column {
                    width: 90
                    spacing: 4
                    FieldLabel { text: "PORT" }
                    RetroTextField {
                        width: parent.width
                        placeholder: "port"
                        text: appConfig.proxyPort > 0 ? String(appConfig.proxyPort) : ""
                        onEdited: function(v) { appConfig.proxyPort = parseInt(v) || 0 }
                    }
                }
            }
            Row {
                width: parent.width
                spacing: 8
                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    FieldLabel { text: "USER (опц.)" }
                    RetroTextField {
                        width: parent.width
                        placeholder: "логин"
                        text: appConfig.proxyUser
                        onEdited: function(v) { appConfig.proxyUser = v.trim() }
                    }
                }
                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    FieldLabel { text: "PASSWORD (опц.)" }
                    RetroTextField {
                        width: parent.width
                        placeholder: "пароль"
                        password: true
                        text: appConfig.proxyPassword
                        onEdited: function(v) { appConfig.proxyPassword = v.trim() }
                    }
                }
            }
            Text {
                width: parent.width
                text: appConfig.proxyEnabled && appConfig.mpvProxyUrl() !== ""
                    ? ("// mpv: " + appConfig.mpvProxyUrl())
                    : "// mpv: без прокси"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.mutedForeground
            }
        }

        // === TORRSERVER ===
        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 8

            FieldLabel { text: "TORRSERVER (обязательно для торрентов)" }
            Row {
                width: parent.width
                spacing: 8
                RetroTextField {
                    id: torrserverField
                    width: parent.width - torrBrowseBtn.width - 8
                    placeholder: "~/Downloads/TorrServer-windows-amd64.exe"
                    text: appConfig.torrServerPath
                    onEdited: function(v) { appConfig.torrServerPath = v }
                }
                RetroButton {
                    id: torrBrowseBtn
                    label: "FIND"
                    onClicked: {
                        const p = appConfig.autoDetectTorrServer()
                        if (p) {
                            torrserverField.text = p
                            appConfig.torrServerPath = p
                        }
                    }
                }
            }
            Row {
                width: parent.width
                spacing: 8
                RetroTextField {
                    id: torrHostField
                    width: parent.width - torrPortField.width - 8
                    placeholder: "127.0.0.1"
                    text: appConfig.torrServerHost
                    onEdited: function(v) { appConfig.torrServerHost = v.trim() }
                }
                RetroTextField {
                    id: torrPortField
                    width: 100
                    placeholder: "8090"
                    text: appConfig.torrServerPort
                    onEdited: function(v) {
                        const p = parseInt(v)
                        if (!isNaN(p)) appConfig.torrServerPort = p
                    }
                }
            }
            Text {
                width: parent.width
                text: "// адрес TorrServer host:port, по умолчанию http://127.0.0.1:8090"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.muted
            }
        }

        // === JACRED / KODIK ===
        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 8

            FieldLabel { text: "JACRED URL" }
            RetroTextField {
                width: parent.width
                placeholder: "https://jac.red"
                text: appConfig.jacredUrl
                onEdited: function(v) { appConfig.jacredUrl = v }
            }

            FieldLabel { text: "KODIK TOKEN" }
            RetroTextField {
                width: parent.width
                placeholder: "вшитый публичный токен"
                text: appConfig.kodikToken
                onEdited: function(v) { appConfig.kodikToken = v.trim() }
            }
            Text {
                width: parent.width
                text: "// в исходниках есть публичный токен по умолчанию. Пустое поле вернёт его. Свой — если этот перестал работать."
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.mutedForeground
                wrapMode: Text.WordWrap
            }
        }

        // === ФИЛЬТР КИТАЯ ===
        Row {
            x: 20
            spacing: 10
            RetroSwitch { on: appConfig.excludeChinese; onToggled: function(v) { appConfig.excludeChinese = v } }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Скрывать китайские аниме"
                font.family: RetroTheme.fontFamily
                font.pixelSize: 11
                color: RetroTheme.foreground
            }
        }

        // === КЭШ ===
        Column {
            x: 20
            width: Math.min(560, parent.width - 40)
            spacing: 8

            FieldLabel { text: "IMAGE CACHE" }
            Row {
                spacing: 10
                RetroButton {
                    label: "CLEAR CACHE"
                    onClicked: {
                        const n = posterCache.clearDiskCache()
                        cacheClearedLabel.text = "// удалено файлов: " + n + " (перезапустите для применения)"
                    }
                }
                Text {
                    id: cacheClearedLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: ""
                    font.family: RetroTheme.fontFamily
                    font.pixelSize: 10
                    color: RetroTheme.mutedForeground
                }
            }
            Text {
                width: parent.width
                text: "// удаляет скачанные постеры и баннеры (включая устаревшие/битые). История просмотра и настройки не затрагиваются."
                font.family: RetroTheme.fontFamily
                font.pixelSize: 10
                color: RetroTheme.mutedForeground
                wrapMode: Text.WordWrap
            }
        }
    }
}
