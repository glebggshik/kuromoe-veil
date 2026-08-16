#pragma once

#include <QObject>
#include <QVariantList>

#include "ShikimoriClient.h"

class BookmarksBridge : public QObject {
    Q_OBJECT
public:
    explicit BookmarksBridge(QObject *parent = nullptr);

    Q_INVOKABLE void loadStatus(const QString &status);

signals:
    void resultsReady(const QVariantList &items);
    void error(const QString &message);

private:
    ShikimoriClient m_client;
};