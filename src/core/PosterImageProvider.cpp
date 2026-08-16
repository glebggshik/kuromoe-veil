#include "PosterImageProvider.h"

#include "PosterCache.h"

PosterImageProvider::PosterImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage PosterImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString path = PosterCache::instance()->filePathForId(id);
    if (path.isEmpty())
        return {};

    QImage image(path);
    if (image.isNull())
        return {};

    if (requestedSize.isValid() && (requestedSize.width() > 0 || requestedSize.height() > 0)) {
        const QSize target = requestedSize.boundedTo(image.size());
        if (target.isValid() && target != image.size())
            image = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (size)
        *size = image.size();
    return image;
}