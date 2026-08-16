#include "HealthCheck.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "AppConfig.h"
#include "NetworkManager.h"

namespace {

constexpr int kCheckTimeoutMs = 8000;

QVariantMap entry(const QString &state, const QString &message) {
    QVariantMap m;
    m[QStringLiteral("state")] = state;
    m[QStringLiteral("message")] = message;
    return m;
}

} // namespace

HealthCheck::HealthCheck(QObject *parent) : QObject(parent) {}

HealthCheck *HealthCheck::instance() {
    static HealthCheck *inst = new HealthCheck(QCoreApplication::instance());
    return inst;
}

void HealthCheck::setResult(const QString &key, const QString &state, const QString &message) {
    if (m_results.value(key).toMap() == entry(state, message))
        return;
    m_results[key] = entry(state, message);
    emit resultsChanged();
}

void HealthCheck::checkAll() {
    m_results.clear();
    m_pending = 0;
    checkTorrServer();
    checkProxy();
    checkKodik();
    checkJacred();
    emit resultsChanged();
}

void HealthCheck::checkTorrServer() {
    const QString key = QStringLiteral("torrserver");
    setResult(key, QStringLiteral("checking"), QStringLiteral("проверяем /echo…"));
    AppConfig *cfg = AppConfig::instance();
    const QString url = QStringLiteral("http://%1:%2/echo")
                            .arg(cfg->torrServerHost())
                            .arg(cfg->torrServerPort());
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(kCheckTimeoutMs);
    QNetworkReply *reply = NetworkManager::instance()->getLocal(req);
    ++m_pending;
    connect(reply, &QNetworkReply::finished, reply, [this, key, reply]() {
        --m_pending;
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        setResult(key, ok ? QStringLiteral("ok") : QStringLiteral("fail"),
                  ok ? QStringLiteral("TorrServer отвечает")
                     : QStringLiteral("нет ответа — проверь путь к exe и host/port в настройках"));
    });
}

void HealthCheck::checkProxy() {
    const QString key = QStringLiteral("proxy");
    AppConfig *cfg = AppConfig::instance();
    if (!cfg->proxyEnabled() || cfg->proxyHost().isEmpty() || cfg->proxyPort() <= 0) {
        setResult(key, QStringLiteral("ok"), QStringLiteral("выключен (потоки идут без прокси)"));
        return;
    }
    setResult(key, QStringLiteral("checking"),
              QStringLiteral("проверяем %1://%2:%3…")
                  .arg(cfg->proxyType(), cfg->proxyHost())
                  .arg(cfg->proxyPort()));
    // api.ipify.org — отдаёт IP вызывающего; через NetworkManager::get()
    // (прокси-осведомлённый менеджер) это и есть проверка самого прокси.
    QNetworkRequest req{QUrl(QStringLiteral("https://api.ipify.org"))};
    req.setTransferTimeout(kCheckTimeoutMs);
    QNetworkReply *reply = NetworkManager::instance()->get(req);
    ++m_pending;
    connect(reply, &QNetworkReply::finished, reply, [this, key, reply]() {
        --m_pending;
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString ip = QString::fromUtf8(reply->readAll()).trimmed();
        reply->deleteLater();
        setResult(key, ok ? QStringLiteral("ok") : QStringLiteral("fail"),
                  ok ? QStringLiteral("прокси работает (IP: %1)").arg(ip)
                     : QStringLiteral("прокси не отвечает — проверь хост/порт/тип, логин/пароль"));
    });
}

void HealthCheck::checkKodik() {
    const QString key = QStringLiteral("kodik");
    setResult(key, QStringLiteral("checking"), QStringLiteral("проверяем токен…"));
    const QString token = AppConfig::instance()->kodikToken();
    QNetworkRequest req{QUrl(QStringLiteral("https://kodik-api.com/list?token=%1&limit=1").arg(token))};
    req.setTransferTimeout(kCheckTimeoutMs);
    // Kodik геоблокирован — через прокси-менеджер, как и обычные запросы.
    QNetworkReply *reply = NetworkManager::instance()->get(req);
    ++m_pending;
    connect(reply, &QNetworkReply::finished, reply, [this, key, reply]() {
        --m_pending;
        const bool netOk = reply->error() == QNetworkReply::NoError;
        const QByteArray data = reply->readAll();
        const QString netError = reply->errorString();
        reply->deleteLater();

        QString apiError;
        if (netOk) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            apiError = root.value(QStringLiteral("error")).toString();
        }
        const bool ok = netOk && apiError.isEmpty();
        setResult(key, ok ? QStringLiteral("ok") : QStringLiteral("fail"),
                  ok ? QStringLiteral("токен работает")
                     : (apiError.isEmpty()
                            ? QStringLiteral("сеть: %1 (нужен прокси?)").arg(netError)
                            : QStringLiteral("API: %1").arg(apiError)));
    });
}

void HealthCheck::checkJacred() {
    const QString key = QStringLiteral("jacred");
    const QString jacredUrl = AppConfig::instance()->jacredUrl();
    setResult(key, QStringLiteral("checking"),
              jacredUrl.isEmpty() ? QStringLiteral("URL не задан")
                                  : QStringLiteral("проверяем %1…").arg(jacredUrl));
    if (jacredUrl.isEmpty()) {
        setResult(key, QStringLiteral("fail"), QStringLiteral("JacRed URL не задан в настройках"));
        return;
    }
    QNetworkRequest req{QUrl(jacredUrl)};
    req.setTransferTimeout(kCheckTimeoutMs);
    // JacRed защищён DDoS-Guard — идём через прокси-менеджер, как поиск.
    QNetworkReply *reply = NetworkManager::instance()->get(req);
    ++m_pending;
    connect(reply, &QNetworkReply::finished, reply, [this, key, reply]() {
        --m_pending;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool netOk = reply->error() == QNetworkReply::NoError;
        const QString netError = reply->errorString();
        reply->deleteLater();
        QString msg;
        if (!netOk)
            msg = QStringLiteral("сеть: %1 (нужен прокси?)").arg(netError);
        else if (status == 200)
            msg = QStringLiteral("доступен");
        else if (status == 429)
            msg = QStringLiteral("429 rate limit — пауза между поисками");
        else
            msg = QStringLiteral("HTTP %1").arg(status);
        setResult(key, (netOk && status == 200) ? QStringLiteral("ok") : QStringLiteral("fail"), msg);
    });
}
