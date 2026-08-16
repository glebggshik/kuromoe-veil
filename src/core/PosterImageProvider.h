#pragma once

#include <QQuickImageProvider>

// Синхронная отдача постеров с диска — без async Image + https в QML.
class PosterImageProvider : public QQuickImageProvider {
public:
    PosterImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};