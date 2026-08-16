#include "PosterThumbnail.h"

#include "PosterCache.h"

#include <limits>

#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

namespace {

QString localImagePath(const QString &source) {
    if (!source.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        return {};
    const QString path = QUrl(source).toLocalFile();
    return QFile::exists(path) ? path : QString();
}

} // namespace

PosterThumbnail::PosterThumbnail(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::Image);

    connect(PosterCache::instance(), &PosterCache::posterReady, this, [this](const QString &remoteUrl) {
        if (remoteUrl == m_posterSource)
            scheduleAsyncReload();
    });
}

PosterThumbnail::~PosterThumbnail() {
    // Отменяем отложенные QtConcurrent::run — иначе invokeMethod в уничтоженный item → 0xC0000005.
    m_loadGeneration = std::numeric_limits<quint64>::max();
}

int PosterThumbnail::currentTargetW() const {
    // ×2 для HiDPI, но не декодируем полноразмерные 600–900px постеры
    // под карточку 168px — это главный раздуватель heap в каталоге.
    const int capped = qMin(360, qMax(96, static_cast<int>(width() * 2)));
    return capped;
}

int PosterThumbnail::currentTargetH() const {
    const int capped = qMin(540, qMax(96, static_cast<int>(height() * 2)));
    return capped;
}

bool PosterThumbnail::haveCurrentImage() const {
    return !m_image.isNull()
        && m_loadedSource == m_posterSource
        && m_loadedW == currentTargetW()
        && m_loadedH == currentTargetH();
}

void PosterThumbnail::setPosterSource(const QString &url) {
    if (m_posterSource == url)
        return;
    m_posterSource = url;
    m_image = {};
    m_loadedSource.clear();
    ++m_loadGeneration;
    emit posterSourceChanged();
    queueDownload();
    update();
}

void PosterThumbnail::setPosterActive(bool on) {
    if (m_posterActive == on)
        return;
    m_posterActive = on;
    emit posterActiveChanged();
    if (!on) {
        // Карточка ушла из cacheBuffer GridView — отдаём QImage (иначе сотни
        // декодированных постеров висят в heap часами → ГБ в «простое»).
        m_image = {};
        m_loadedSource.clear();
        m_loadedW = 0;
        m_loadedH = 0;
        ++m_loadGeneration;
        update();
        return;
    }
    // Картинка уже на месте (вернулись на вкладку) — не трогаем диск/сеть.
    if (haveCurrentImage())
        return;
    queueDownload();
}

void PosterThumbnail::setRequestPriority(bool on) {
    if (m_requestPriority == on)
        return;
    m_requestPriority = on;
    emit requestPriorityChanged();
    queueDownload();
}

void PosterThumbnail::setPlaceholderLetter(const QString &letter) {
    if (m_placeholderLetter == letter)
        return;
    m_placeholderLetter = letter.isEmpty() ? QStringLiteral("?") : letter;
    emit placeholderLetterChanged();
    update();
}

void PosterThumbnail::setCornerRadius(qreal radius) {
    if (qFuzzyCompare(m_cornerRadius, radius))
        return;
    m_cornerRadius = radius;
    emit cornerRadiusChanged();
    update();
}

void PosterThumbnail::setAspectCrop(bool on) {
    if (m_aspectCrop == on)
        return;
    m_aspectCrop = on;
    emit aspectCropChanged();
    m_image = {};
    ++m_loadGeneration;
    scheduleAsyncReload();
}

void PosterThumbnail::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        scheduleAsyncReload();
}

QImage PosterThumbnail::loadScaledImage(const QString &path, int targetW, int targetH, bool aspectCrop) {
    QImage loaded(path);
    if (loaded.isNull())
        return {};
    const int w = qMax(1, targetW);
    const int h = qMax(1, targetH);
    if (aspectCrop) {
        loaded = loaded.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (loaded.width() - w) / 2;
        const int y = (loaded.height() - h) / 2;
        if (x >= 0 && y >= 0 && loaded.width() >= w && loaded.height() >= h)
            return loaded.copy(x, y, w, h);
        return loaded;
    }
    if (w > 96 || h > 96)
        loaded = loaded.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return loaded;
}

void PosterThumbnail::queueDownload() {
    if (!m_posterActive || m_posterSource.isEmpty())
        return;

    scheduleAsyncReload();

    if (!localImagePath(m_posterSource).isEmpty())
        return;

    if (!PosterCache::instance()->isRemotePoster(m_posterSource))
        return;

    // Раньше здесь был синхронный hasCached() — он декодирует JPEG через
    // QImageReader::read() для валидации, и на активации вкладки это
    // выполнялось для всех видимых постеров подряд в UI-потоке (главный
    // источник ~1.3 с фриза). Вместо этого просто ставим запрос в очередь:
    // PosterCache::request() сам дёшево проверит наличие файла (posterReady
    // без скачивания, если уже на диске), а scheduleAsyncReload() выше и так
    // подхватит локальный файл. Тяжёлую валидацию с декодом делает уже
    // фоновый путь PosterCache, не GUI-поток.
    if (m_requestPriority)
        PosterCache::instance()->requestPriority(m_posterSource);
    else
        PosterCache::instance()->request(m_posterSource);
}

void PosterThumbnail::scheduleAsyncReload() {
    if (!m_posterActive || m_posterSource.isEmpty()) {
        if (!m_image.isNull()) {
            m_image = {};
            update();
        }
        return;
    }

    // Уже загружено под текущий источник и размер — не гоняем декод повторно
    // (главный выигрыш при возврате на вкладку).
    if (haveCurrentImage())
        return;

    QString path = localImagePath(m_posterSource);
    if (path.isEmpty()) {
        const QString id = PosterCache::instance()->posterId(m_posterSource);
        if (id.isEmpty())
            return;
        path = PosterCache::instance()->filePathForId(id);
        if (path.isEmpty())
            return;
    }

    const QString source = m_posterSource;
    const int targetW = currentTargetW();
    const int targetH = currentTargetH();
    const quint64 generation = ++m_loadGeneration;
    const bool aspectCrop = m_aspectCrop;

    QPointer<PosterThumbnail> self(this);
    (void)QtConcurrent::run([self, generation, path, source, targetW, targetH, aspectCrop]() {
        if (!self)
            return;
        const QImage loaded = loadScaledImage(path, targetW, targetH, aspectCrop);
        QTimer::singleShot(0, self, [self, generation, loaded, source, targetW, targetH]() {
            if (!self)
                return;
            self->applyLoadedImage(generation, loaded, source, targetW, targetH);
        });
    });
}

void PosterThumbnail::applyLoadedImage(quint64 generation, const QImage &image,
                                       const QString &source, int targetW, int targetH) {
    if (generation != m_loadGeneration || image.isNull())
        return;
    if (image.cacheKey() == m_image.cacheKey())
        return;
    m_image = image;
    m_loadedSource = source;
    m_loadedW = targetW;
    m_loadedH = targetH;
    update();
}

void PosterThumbnail::paint(QPainter *painter) {
    const QRectF bounds = boundingRect();
    if (m_cornerRadius > 0) {
        QPainterPath clip;
        clip.addRoundedRect(bounds, m_cornerRadius, m_cornerRadius);
        painter->setClipPath(clip);
    }

    if (!m_image.isNull()) {
        painter->drawImage(bounds, m_image);
        return;
    }

    QLinearGradient grad(bounds.topLeft(), bounds.bottomLeft());
    grad.setColorAt(0.0, QColor(QStringLiteral("#2a2a3d")));
    grad.setColorAt(1.0, QColor(QStringLiteral("#1a1a28")));
    painter->fillRect(bounds, grad);

    if (m_placeholderLetter.isEmpty())
        return;

    painter->setPen(QColor(QStringLiteral("#55ffffff")));
    QFont font = painter->font();
    font.setPixelSize(qMax(18, static_cast<int>(qMin(bounds.width(), bounds.height()) * 0.35)));
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(bounds, Qt::AlignCenter, m_placeholderLetter);
}