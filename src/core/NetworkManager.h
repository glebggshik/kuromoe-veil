#pragma once

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QObject>
#include <memory>

#include "AppConfig.h"

// Единая точка сетевых запросов к API (Kodik/JacRed/AniLibria/Shikimori).
// Прокси применяется глобально и перечитывается из AppConfig при каждом
// изменении настроек — никаких захардкоженных адресов.
//
// TorrServer всегда на localhost — для него прокси НЕ применяется (см.
// requestForLocal), иначе локальные запросы рвутся через внешний прокси.
class NetworkManager : public QObject {
    Q_OBJECT
public:
    static NetworkManager *instance();

    // PosterCache: без transferTimeout (0), только детект зависания по скорости.
    static constexpr QNetworkRequest::Attribute ImageDownloadRequest =
        static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 1);

    QNetworkAccessManager *manager() { return external(); }

    // GET/POST с применённым (или отсутствующим) прокси согласно AppConfig.
    QNetworkReply *get(const QNetworkRequest &request);
    QNetworkReply *post(const QNetworkRequest &request, const QByteArray &body);

    // Для TorrServer и любого другого localhost-сервиса — всегда без прокси.
    QNetworkReply *getLocal(const QNetworkRequest &request);
    QNetworkReply *postLocal(const QNetworkRequest &request, const QByteArray &body);

    // Останавливает http-потоки QNAM (join в деструкторе QNAM).
    // aboutToQuit вызывает это до разборки QGuiApplication; после shutdown()
    // менеджеры лениво пересоздаются, если кто-то сделает поздний запрос.
    void shutdown();

private slots:
    void refreshProxy();

private:
    explicit NetworkManager(QObject *parent = nullptr);

    QNetworkAccessManager *external();
    QNetworkAccessManager *local();

    // Два отдельных QNetworkAccessManager вместо переключения proxy на одном
    // перед каждым запросом — иначе параллельные запросы (внешний API и
    // TorrServer одновременно) гонялись бы за общим proxy-состоянием.
    std::unique_ptr<QNetworkAccessManager> m_namExternal; // прокси из AppConfig (Kodik/JacRed/AniLibria)
    std::unique_ptr<QNetworkAccessManager> m_namLocal;    // NoProxy: TorrServer, AniList/Shikimori CDN
};
