#include "StreamReadiness.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "NetworkManager.h"

namespace {

bool isHlsUrl(const QString &url) {
    return url.contains(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
}

QNetworkReply *probe(const QString &url, StreamReadiness::Route route, const QString &referer) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    // Referer как в MpvPlayer::playUrl: CVH-CDN (okcdn/vkuser/mycdn) — animego.org,
    // иначе — переданный с вызывающей стороны; пустой = не слать (неверный
    // referer хуже отсутствующего: сервер может ответить 403).
    const bool cvhCdn = url.contains(QStringLiteral("okcdn.ru"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("vkuser.net"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("mycdn.me"), Qt::CaseInsensitive);
    if (cvhCdn)
        req.setRawHeader("Referer", "https://animego.org/");
    else if (!referer.isEmpty())
        req.setRawHeader("Referer", referer.toUtf8());
    // HLS-плейлисты (okcdn/vkuser) часто отвечают 400 на Range — только GET.
    if (!isHlsUrl(url)) {
        req.setRawHeader("Range", "bytes=0-1");
    }
    return route == StreamReadiness::Route::Local
               ? NetworkManager::instance()->getLocal(req)
               : NetworkManager::instance()->get(req);
}

}

void StreamReadiness::waitUntilReady(
    const QString &url,
    Route route,
    const QString &referer,
    std::function<void(bool, int)> callback,
    int maxAttempts,
    int intervalMs) {
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [url, route, referer, callback, intervalMs, attempt](int attemptsLeft) {
        QNetworkReply *reply = probe(url, route, referer);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url, route, referer, callback, intervalMs, attempt, attemptsLeft]() {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bool ok = (status == 200 || status == 206);
            reply->deleteLater();

            if (ok) {
                callback(true, status);
                return;
            }
            if (attemptsLeft <= 1) {
                callback(false, status);
                return;
            }
            QTimer::singleShot(intervalMs, [attempt, attemptsLeft]() { (*attempt)(attemptsLeft - 1); });
        });
    };
    (*attempt)(maxAttempts);
}
