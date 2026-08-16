#include "JacRedClient.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include "AppConfig.h"
#include "NetworkManager.h"

QString JacRedClient::humanSize(qint64 bytes) {
    if (bytes <= 0)
        return "?";
    double size = static_cast<double>(bytes);
    const QStringList units = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    for (int i = 0; i < units.size(); ++i) {
        if (size < 1024.0 || i == units.size() - 1)
            return QString::number(size, 'f', 1) + " " + units[i];
        size /= 1024.0;
    }
    return QString();
}

void JacRedClient::search(const QString &title, std::function<void(QVariantList, QString, int)> callback) {
    QString base = AppConfig::instance()->jacredUrl();
    if (base.isEmpty())
        base = "https://jac.red";

    QUrl url(base + "/api/v2.0/indexers/all/results");
    QUrlQuery query;
    query.addQueryItem("Query", title);
    url.setQuery(query);

    // jac.red — публичный агрегатор; в браузере открывается напрямую, без прокси.
    // Через прокси из настроек запросы часто падают в пустоту.
    QNetworkReply *reply = NetworkManager::instance()->getLocal(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback, title]() {
        QByteArray data = reply->readAll();
        QNetworkReply::NetworkError err = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (err != QNetworkReply::NoError) {
            qWarning("JacRedClient: network error for '%s' — HTTP %d — %s",
                      qUtf8Printable(title), httpStatus, qUtf8Printable(reply->errorString()));
            // Как и в Python-версии — сетевая ошибка JacRed не должна
            // показываться пользователю как фатальная: просто пусто. Код
            // статуса пробрасываем дальше — вызывающий код (searchTorrents)
            // отдельно реагирует на 429 (rate limit) увеличенным backoff'ом,
            // а не как на "раздачи не найдены".
            callback({}, QString(), httpStatus);
            return;
        }
        QJsonArray results = QJsonDocument::fromJson(data).object().value("Results").toArray();
        QVariantList out;
        for (const QJsonValue &rv : results) {
            QJsonObject t = rv.toObject();
            QString magnet = t.value("MagnetUri").toString();
            if (magnet.isEmpty())
                continue;
            QVariantMap item;
            item["title"] = t.contains("Title") ? t.value("Title").toString() : title;
            item["magnet"] = magnet;
            item["size"] = humanSize(static_cast<qint64>(t.value("Size").toDouble()));
            item["seeders"] = t.value("Seeders").toInt();
            item["tracker"] = t.value("Tracker").toString();
            out << item;
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("seeders").toInt() > b.toMap().value("seeders").toInt();
        });
        callback(out, QString(), httpStatus);
    });
}
