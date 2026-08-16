#pragma once

#include <QObject>
#include <QVariantList>
#include <functional>

// Порт core/jacred_client.py — поиск торрентов через JacRed (публичный
// инстанс jac.red по умолчанию, или свой сервер через AppConfig::jacredUrl).
// v2.0/indexers/all/results — Jackett-совместимый эндпоинт, отдаёт сразу с
// нескольких трекеров (rutor, kinozal, nnmclub, toloka и т.п.) без apikey.
class JacRedClient : public QObject {
    Q_OBJECT
public:
    explicit JacRedClient(QObject *parent = nullptr) : QObject(parent) {}

    // [{title, magnet, size, seeders, tracker}], отсортировано по seeders убыв.
    // httpStatus — реальный код ответа (0, если не дошло до HTTP-уровня —
    // обрыв/таймаут соединения); нужен, чтобы отличать rate-limit (429) от
    // обычной сетевой ошибки и реагировать на них по-разному (см. searchTorrents
    // в DetailBridge.cpp — backoff только на 429).
    void search(const QString &title, std::function<void(QVariantList, QString, int)> callback);

private:
    static QString humanSize(qint64 bytes);
};
