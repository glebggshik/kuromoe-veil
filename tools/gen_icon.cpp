#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QFile>
#include <QBuffer>

namespace {

bool writeIco(const QString &path, const QList<QImage> &images) {
    if (images.isEmpty())
        return false;

    struct Entry {
        quint8 width = 0;
        quint8 height = 0;
        QByteArray png;
    };

    QList<Entry> entries;
    entries.reserve(images.size());
    for (const QImage &src : images) {
        Entry entry;
        entry.width = src.width() >= 256 ? 0 : static_cast<quint8>(src.width());
        entry.height = src.height() >= 256 ? 0 : static_cast<quint8>(src.height());
        QBuffer buffer(&entry.png);
        buffer.open(QIODevice::WriteOnly);
        if (!src.save(&buffer, "PNG"))
            return false;
        entries << entry;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    auto write16 = [&file](quint16 v) {
        file.putChar(static_cast<char>(v & 0xff));
        file.putChar(static_cast<char>((v >> 8) & 0xff));
    };
    auto write32 = [&file](quint32 v) {
        for (int i = 0; i < 4; ++i)
            file.putChar(static_cast<char>((v >> (8 * i)) & 0xff));
    };

    file.putChar(0);
    file.putChar(0);
    write16(1);
    write16(static_cast<quint16>(entries.size()));

    quint32 offset = 6 + static_cast<quint32>(entries.size()) * 16;
    for (const Entry &entry : entries) {
        file.putChar(static_cast<char>(entry.width));
        file.putChar(static_cast<char>(entry.height));
        file.putChar(0);
        file.putChar(0);
        write16(1);
        write16(32);
        write32(static_cast<quint32>(entry.png.size()));
        write32(offset);
        offset += static_cast<quint32>(entry.png.size());
    }
    for (const Entry &entry : entries)
        file.write(entry.png);
    return true;
}

QImage renderSvg(QSvgRenderer &renderer, int size) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();
    return img;
}

} // namespace

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    const QString svgPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("qml/assets/logo.svg");
    const QString pngPath = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                     : QStringLiteral("resources/app.png");
    const QString icoPath = argc > 3 ? QString::fromLocal8Bit(argv[3])
                                     : QStringLiteral("resources/app.ico");
    const QString qmlPngPath = argc > 4 ? QString::fromLocal8Bit(argv[4])
                                        : QStringLiteral("qml/assets/app.png");

    QSvgRenderer renderer(svgPath);
    if (!renderer.isValid())
        return 1;

    const QImage png512 = renderSvg(renderer, 512);
    if (!png512.save(pngPath, "PNG"))
        return 2;
    if (!png512.save(qmlPngPath, "PNG"))
        return 3;

    QList<QImage> icoImages;
    for (const int size : {16, 32, 48, 64, 128, 256})
        icoImages << renderSvg(renderer, size);
    if (!writeIco(icoPath, icoImages))
        return 4;

    return 0;
}