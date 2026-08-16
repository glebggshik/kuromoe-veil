#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <functional>

#include "HentaiSiteCommon.h"

// Парсер AniStar (astar.bz). MP4 в HTML; site-торренты — через /engine/gettorrent.php.
class AnistarClient : public QObject {
    Q_OBJECT
public:
    explicit AnistarClient(QObject *parent = nullptr) : QObject(parent) {}

    using TranslationsCallback = std::function<void(QVariantList translations, QString error)>;
    using StreamCallback = std::function<void(QString url, QString error)>;
    using TorrentsCallback = std::function<void(QVariantList torrents, QString error)>;

    void loadTranslations(const QString &title, const QString &originalTitle,
                          const QString &englishTitle, const QString &japaneseTitle, int year,
                          TranslationsCallback callback);

    void getEpisodeStream(const QString &translationId, int episode, StreamCallback callback);

    void fetchPageTorrents(const QString &title, const QString &originalTitle,
                           const QString &englishTitle, const QString &japaneseTitle, int year,
                           TorrentsCallback callback);

    QStringList defaultMirrors() const;
    QStringList mirrorSeeds() const;

private:
    void resolveMirrors(std::function<void(QStringList mirrors)> callback);
    void resolvePage(const QStringList &queries, int year,
                     std::function<void(QString pagePath, QString mirrorBase, QString err)> callback);

    QStringList m_mirrors;
    QString m_activeMirror;
    QHash<QString, QHash<int, QString>> m_episodeUrls;
    QString m_lastPagePath;
};