#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// Источник Animetka (animetka.com / notanime.ru): JSON API каталога +
// playlist с озвучками и мульти-качеством (HLS / Kodik / AniLibria mapping).
// id озвучек в UI: "animetka_<tid>" или "animetka_<tid>_q1080".
class AnimetkaClient : public QObject {
    Q_OBJECT
public:
    explicit AnimetkaClient(QObject *parent = nullptr) : QObject(parent) {}

    using TranslationsCallback = std::function<void(QVariantList translations, QString error)>;
    using StreamCallback = std::function<void(QString url, QString error)>;

    // shikimoriId + названия/год — для поиска material id на Animetka.
    void loadTranslations(const QString &shikimoriId,
                          const QString &title,
                          const QString &originalTitle,
                          const QString &englishTitle,
                          int year,
                          TranslationsCallback callback);

    // translationId: "animetka_610" или "animetka_610_q1080"
    // qualityHint: "1080"/"720"/… или пусто (= best available)
    void getEpisodeStream(const QString &translationId,
                          int episode,
                          const QString &qualityHint,
                          StreamCallback callback);

    // Разбор "animetka_<tid>" / "animetka_<tid>_q1080" → tid + quality.
    static bool parseTranslationId(const QString &id, QString *tidOut, QString *qualityOut = nullptr);
    static QString makeTranslationId(const QString &tid, const QString &quality = {});

private:
    QString m_materialId;
    // tid → material id (на случай смены)
    QVariantMap m_tidToMaterial;
};
