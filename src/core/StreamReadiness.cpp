#include "StreamReadiness.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "NetworkManager.h"

namespace {

bool isHlsUrl(const QString &url) {
    return url.contains(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
}

QNetworkReply *probe(const QString &url, StreamReadiness::Route route) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    req.setRawHeader("Referer", "https://animego.org/");
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
    std::function<void(bool, int)> callback,
    int maxAttempts,
    int intervalMs) {
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [url, route, callback, intervalMs, attempt](int attemptsLeft) {
        QNetworkReply *reply = probe(url, route);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url, route, callback, intervalMs, attempt, attemptsLeft]() {
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
