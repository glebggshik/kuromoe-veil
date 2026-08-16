#include "AniLibriaClient.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include "NetworkManager.h"

namespace {
const QString kBase = "https://anilibria.top/api/v1";

QVariantMap toMap(const QByteArray &json) {
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

QVariantList toList(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}
}

void AniLibriaClient::searchByTitle(const QString &title, std::function<void(QVariantList, QString)> callback) {
    QUrl url(kBase + "/app/search/releases");
    QUrlQuery query;
    query.addQueryItem("query", title);
    url.setQuery(query);

    QNetworkReply *reply = NetworkManager::instance()->get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback]() {
        QByteArray data = reply->readAll();
        QNetworkReply::NetworkError err = reply->error();
        reply->deleteLater();
        if (err != QNetworkReply::NoError) {
            qWarning("AniLibria: search failed — %s", qUtf8Printable(reply->errorString()));
            callback({}, reply->errorString());
            return;
        }
        callback(toList(data), QString());
    });
}

void AniLibriaClient::getRelease(int releaseId, ReleaseCallback callback) {
    QUrl url(kBase + QString("/anime/releases/%1").arg(releaseId));
    QNetworkReply *reply = NetworkManager::instance()->get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback]() {
        QByteArray data = reply->readAll();
        QNetworkReply::NetworkError err = reply->error();
        reply->deleteLater();
        if (err != QNetworkReply::NoError) {
            qWarning("AniLibria: getRelease failed — %s", qUtf8Printable(reply->errorString()));
            callback({}, reply->errorString());
            return;
        }
        callback(toMap(data), QString());
    });
}

QVariantMap AniLibriaClient::bestMatch(
    const QVariantList &releases, const QString &title, const QString &originalTitle,
    const QString &kind, int year) {
    static const QMap<QString, QSet<QString>> kindGroups = {
        {"tv", {"TV"}}, {"tv_special", {"TV", "TV_SPECIAL"}},
        {"movie", {"MOVIE"}}, {"ova", {"OVA"}}, {"ona", {"ONA"}}, {"special", {"TV_SPECIAL", "OVA"}},
    };

    QSet<QString> wanted;
    if (!title.trimmed().isEmpty())
        wanted << title.trimmed().toLower();
    if (!originalTitle.trimmed().isEmpty())
        wanted << originalTitle.trimmed().toLower();
    QSet<QString> allowedTypes = kindGroups.value(kind);

    for (const QVariant &rv : releases) {
        QVariantMap r = rv.toMap();
        QVariantMap name = r.value("name").toMap();
        QSet<QString> candidates = {
            name.value("main").toString().trimmed().toLower(),
            name.value("english").toString().trimmed().toLower(),
            name.value("alternative").toString().trimmed().toLower(),
        };
        if ((wanted & candidates).isEmpty())
            continue;
        QString releaseType = r.value("type").toMap().value("value").toString();
        if (!allowedTypes.isEmpty() && !releaseType.isEmpty() && !allowedTypes.contains(releaseType))
            continue;
        int releaseYear = r.value("year").toInt();
        if (year && releaseYear && releaseYear != year)
            continue;
        return r;
    }
    return {};
}

void AniLibriaClient::findRelease(
    const QString &title, const QString &originalTitle,
    const QString &kind, int year, ReleaseCallback callback) {
    searchByTitle(title, [this, title, originalTitle, kind, year, callback](QVariantList results, QString error) {
        if (!error.isEmpty() || results.isEmpty()) {
            callback({}, QString());
            return;
        }
        QVariantMap match = bestMatch(results, title, originalTitle, kind, year);
        if (match.isEmpty()) {
            callback({}, QString());
            return;
        }
        int releaseId = match.value("id").toInt();
        getRelease(releaseId, [callback](QVariantMap release, QString err) {
            callback(err.isEmpty() ? release : QVariantMap(), QString());
        });
    });
}

QVariantList AniLibriaClient::getEpisodes(const QVariantMap &release) {
    QVariantList episodes;
    for (const QVariant &ev : release.value("episodes").toList()) {
        QVariantMap ep = ev.toMap();
        QVariantMap qualities;
        for (int q : {480, 720, 1080}) {
            QString url = ep.value(QString("hls_%1").arg(q)).toString();
            if (!url.isEmpty())
                qualities[QString::number(q)] = url;
        }
        QVariantMap out;
        out["episode"] = ep.value("ordinal");
        out["qualities"] = qualities;
        episodes << out;
    }
    std::sort(episodes.begin(), episodes.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("episode").toInt() < b.toMap().value("episode").toInt();
    });
    return episodes;
}

QString AniLibriaClient::getEpisodeStream(const QVariantMap &release, int episode) {
    for (const QVariant &ev : getEpisodes(release)) {
        QVariantMap ep = ev.toMap();
        if (ep.value("episode").toInt() != episode)
            continue;
        QVariantMap qualities = ep.value("qualities").toMap();
        for (int q : {1080, 720, 480}) {
            QString key = QString::number(q);
            if (qualities.contains(key))
                return qualities.value(key).toString();
        }
    }
    return QString();
}

QVariantList AniLibriaClient::getTorrents(const QVariantMap &release) {
    QVariantList out;
    for (const QVariant &tv : release.value("torrents").toList()) {
        QVariantMap t = tv.toMap();
        QVariantMap item;
        item["quality"] = t.value("quality").toMap().value("value");
        item["seeders"] = t.value("seeders");
        item["sizeGb"] = qRound((t.value("size").toDouble() / (1024.0 * 1024.0 * 1024.0)) * 100) / 100.0;
        item["magnet"] = t.value("magnet");
        item["label"] = t.value("label");
        out << item;
    }
    return out;
}
