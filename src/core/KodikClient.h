#pragma once

#include <QObject>
#include <QVariantList>
#include <functional>

// Порт core/video_source.py + anime_parsers_ru.KodikParser — озвучки Kodik
// по shikimori_id и HLS-ссылки на серии. Токен кэшируется в AppConfig;
// для get_info/get_link нужен прокси из настроек (геоблок Kodik).
class KodikClient : public QObject {
    Q_OBJECT
public:
    explicit KodikClient(QObject *parent = nullptr) : QObject(parent) {}

    using TranslationsCallback = std::function<void(QVariantList translations, QString error)>;
    using StreamCallback = std::function<void(QString url, QString error)>;

    void loadTranslations(const QString &shikimoriId, TranslationsCallback callback);
    void getEpisodeStream(
        const QString &shikimoriId, int episode, const QString &translationId,
        StreamCallback callback);
};