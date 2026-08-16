#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>

// Миниатюра постера без QML Image — в MSVC Debug сотни Image + image://
// дают abort() из-за потоков scene graph / QThreadStorage.
class PosterThumbnail : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString posterSource READ posterSource WRITE setPosterSource NOTIFY posterSourceChanged)
    Q_PROPERTY(bool posterActive READ posterActive WRITE setPosterActive NOTIFY posterActiveChanged)
    Q_PROPERTY(bool requestPriority READ requestPriority WRITE setRequestPriority NOTIFY requestPriorityChanged)
    Q_PROPERTY(QString placeholderLetter READ placeholderLetter WRITE setPlaceholderLetter NOTIFY placeholderLetterChanged)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)
    Q_PROPERTY(bool aspectCrop READ aspectCrop WRITE setAspectCrop NOTIFY aspectCropChanged)

public:
    explicit PosterThumbnail(QQuickItem *parent = nullptr);
    ~PosterThumbnail() override;

    QString posterSource() const { return m_posterSource; }
    void setPosterSource(const QString &url);

    bool posterActive() const { return m_posterActive; }
    void setPosterActive(bool on);

    bool requestPriority() const { return m_requestPriority; }
    void setRequestPriority(bool on);

    QString placeholderLetter() const { return m_placeholderLetter; }
    void setPlaceholderLetter(const QString &letter);

    qreal cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(qreal radius);

    bool aspectCrop() const { return m_aspectCrop; }
    void setAspectCrop(bool on);

    void paint(QPainter *painter) override;

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

signals:
    void posterSourceChanged();
    void posterActiveChanged();
    void requestPriorityChanged();
    void placeholderLetterChanged();
    void cornerRadiusChanged();
    void aspectCropChanged();

private:
    static QImage loadScaledImage(const QString &path, int targetW, int targetH, bool aspectCrop);

    void queueDownload();
    void scheduleAsyncReload();
    void applyLoadedImage(quint64 generation, const QImage &image, const QString &source,
                          int targetW, int targetH);
    // Изображение уже загружено под текущий источник и близкий размер — тогда
    // повторная активация вкладки (posterActive false→true) не должна ничего
    // перезапускать: именно это давало ~1.3 с блокировки UI на каждый заход
    // на Главную/Обзор (50 постеров × синхронная проверка/декод).
    bool haveCurrentImage() const;
    int currentTargetW() const;
    int currentTargetH() const;

    QString m_posterSource;
    QString m_placeholderLetter = QStringLiteral("?");
    bool m_posterActive = true;
    bool m_requestPriority = false;
    QImage m_image;
    QString m_loadedSource;   // источник, под который загружен m_image
    int m_loadedW = 0;        // целевой размер, под который отмасштабирован m_image
    int m_loadedH = 0;
    qreal m_cornerRadius = 10;
    bool m_aspectCrop = false;
    quint64 m_loadGeneration = 0;
};