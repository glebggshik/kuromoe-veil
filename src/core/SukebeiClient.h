#pragma once

#include <QObject>
#include <QVariantList>
#include <functional>

// Поиск торрентов на sukebei.nyaa.si — основной источник раздач для 18+ аниме.
// JacRed (rutor/kinozal/…) хентай почти не индексирует; Kodik/CVH — выборочно.
class SukebeiClient : public QObject {
    Q_OBJECT
public:
    explicit SukebeiClient(QObject *parent = nullptr) : QObject(parent) {}

    // [{title, magnet, size, seeders, tracker}], отсортировано по seeders убыв.
    // httpStatus — см. комментарий в JacRedClient.h (0 = не дошло до HTTP).
    void search(const QString &title, std::function<void(QVariantList, QString, int)> callback);

private:
    static QString humanSize(qint64 bytes);
};