#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// Порт core/anilibria_client.py — AniLibria API v1 (anilibria.top). Покрывает
// только то, что озвучено самим AniLibria (не весь каталог, в отличие от
// Kodik), но даёт честный HLS до 1080p и торренты без геоблоков/авторизации.
class AniLibriaClient : public QObject {
    Q_OBJECT
public:
    explicit AniLibriaClient(QObject *parent = nullptr) : QObject(parent) {}

    using ReleaseCallback = std::function<void(QVariantMap release, QString error)>;

    // Ищет релиз по названию (с проверкой типа/года — см. _bestMatch) и сразу
    // подгружает полную карточку (эпизоды/торренты). release.isEmpty(), если
    // точного совпадения не нашлось (а не просто "ничего не нашли вовсе").
    void findRelease(
        const QString &title, const QString &originalTitle,
        const QString &kind, int year, ReleaseCallback callback);

    // [{episode:int, qualities:{480:url,720:url,1080:url}}]
    static QVariantList getEpisodes(const QVariantMap &release);

    // Ссылка на лучшее доступное качество для конкретной серии, или "".
    static QString getEpisodeStream(const QVariantMap &release, int episode);

    // [{quality, seeders, sizeGb, magnet, label}]
    static QVariantList getTorrents(const QVariantMap &release);

private:
    void searchByTitle(const QString &title, std::function<void(QVariantList, QString)> callback);
    void getRelease(int releaseId, ReleaseCallback callback);
    static QVariantMap bestMatch(
        const QVariantList &releases, const QString &title, const QString &originalTitle,
        const QString &kind, int year);
};
