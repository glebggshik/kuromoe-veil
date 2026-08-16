#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

struct HentaiSearchHit {
    QString title;
    QString pageUrl;
    QString mirrorBase;
    int score = 0;
};

struct HentaiVoiceTrack {
    QString id;
    QString title;
    int episodes = 0;
    QHash<int, QString> episodeUrls;
};

using HentaiBytesCallback = std::function<void(QByteArray body, QString mirrorBase, QString error)>;

namespace HentaiSiteCommon {

QString htmlBodyToString(const QByteArray &body);
QString decodeHtmlEntities(const QString &text);
QString normalizeSearchText(QString text);
QStringList searchTokens(const QString &text);
int scoreSearchField(const QString &field, const QStringList &queries);
int scoreSearchHit(const HentaiSearchHit &hit, const QStringList &queries, int year);

QList<HentaiSearchHit> parseDleSearchHits(const QByteArray &html, const QString &mirrorBase);
QList<HentaiVoiceTrack> parseMp4VoiceTracks(const QString &html);

QStringList buildSearchQueries(const QString &title, const QString &originalTitle,
                               const QString &englishTitle, const QString &japaneseTitle);

void fetchFirstMirror(const QStringList &mirrors, const QString &path, bool useProxy,
                      HentaiBytesCallback callback);

QStringList parseMirrorsFromHtml(const QByteArray &html);
QStringList mergeMirrorLists(const QStringList &preferred, const QStringList &extra);

using HentaiMirrorsCallback = std::function<void(QStringList mirrors)>;

void discoverMirrors(const QStringList &seeds, const QStringList &fallback, bool useProxy,
                     HentaiMirrorsCallback callback);

QVariantList tracksToTranslations(const QList<HentaiVoiceTrack> &tracks, const QString &idPrefix,
                                  const QString &labelPrefix);

} // namespace HentaiSiteCommon