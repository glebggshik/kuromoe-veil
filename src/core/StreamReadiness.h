#pragma once

#include <QString>
#include <functional>

// Общая проверка готовности медиапотока ДО передачи его в libmpv.
//
// Раньше (Python-версия) только торренты ждали буферизацию — прямые
// ссылки (Kodik m3u8, геоблокированные) отдавались в mpv "на слово", и
// именно там тоже ловились зависания/крэши плеера (мёртвый m3u8, сервер
// ещё не успел ответить, прокси не поднялся). Здесь проверка одна для
// всех источников: торрент-стрим (TorrentStreamManager) и прямые ссылки
// (Kodik/AniLibria, через PlaybackController) проходят через один и тот
// же поллинг с одинаковыми гарантиями.
class StreamReadiness {
public:
    enum class Route {
        Local,    // TorrServer на localhost — без прокси (NetworkManager::getLocal)
        External, // Kodik/AniLibria — с прокси из AppConfig (NetworkManager::get)
    };

    // Опрашивает url, пока не получит подтверждение готовности (200/206 и
    // непустое тело) либо не кончится число попыток. callback(ok, httpStatus)
    // вызывается ровно один раз. Не блокирует — все попытки асинхронные.
    // referer — с вызывающей стороны (как в MpvPlayer::playUrl): для CVH-CDN
    // используется animego.org, иначе — переданный, пустой = не слать.
    static void waitUntilReady(
        const QString &url,
        Route route,
        const QString &referer,
        std::function<void(bool ok, int httpStatus)> callback,
        int maxAttempts = 60,
        int intervalMs = 500);
};
