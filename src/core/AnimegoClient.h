#pragma once

#include <QObject>
#include <QVariantList>
#include <functional>

// AnimeGO + плеер CVH (CdnVideoHub). Порт anime_parsers_ru.AnimegoParser (только CVH).
// Поиск/плеер — animego.org (может требовать прокси). Потоки — plapi.cdnvideohub.com.
class AnimegoClient : public QObject {
    Q_OBJECT
public:
    explicit AnimegoClient(QObject *parent = nullptr) : QObject(parent) {}

    using TranslationsCallback = std::function<void(QVariantList translations, QString error)>;
    using StreamCallback = std::function<void(QString url, QString error)>;

    void loadTranslations(
        const QString &title, const QString &originalTitle, const QString &englishTitle,
        const QString &kind, int year, TranslationsCallback callback);

    void getEpisodeStream(
        const QString &animegoId, int episode, const QString &translationId,
        StreamCallback callback);
};