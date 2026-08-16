#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <functional>

#include "HentaiSiteCommon.h"

// Парсер hentasis*.top — прямые MP4 в HTML. Зеркала перебираются автоматически.
class HentasisClient : public QObject {
    Q_OBJECT
public:
    explicit HentasisClient(QObject *parent = nullptr) : QObject(parent) {}

    using TranslationsCallback = std::function<void(QVariantList translations, QString error)>;
    using StreamCallback = std::function<void(QString url, QString error)>;

    void loadTranslations(const QString &title, const QString &originalTitle,
                          const QString &englishTitle, const QString &japaneseTitle, int year,
                          TranslationsCallback callback);

    void getEpisodeStream(const QString &translationId, int episode, StreamCallback callback);

    QStringList defaultMirrors() const;
    QStringList mirrorSeeds() const;

private:
    void resolveMirrors(std::function<void(QStringList mirrors)> callback);
    QStringList m_mirrors;
    QString m_activeMirror;
    QHash<QString, QHash<int, QString>> m_episodeUrls;
};