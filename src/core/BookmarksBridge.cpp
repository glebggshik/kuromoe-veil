#include "BookmarksBridge.h"

#include <QMap>
#include <QPointer>

#include "StatusStore.h"

BookmarksBridge::BookmarksBridge(QObject *parent)
    : QObject(parent), m_client(this) {}

void BookmarksBridge::loadStatus(const QString &status) {
    const QVariantList cached = StatusStore::instance()->listByStatus(status);
    if (cached.isEmpty()) {
        emit resultsReady(cached);
        return;
    }

    QStringList ids;
    for (const QVariant &v : cached) {
        const QString id = v.toMap().value("id").toString();
        if (!id.isEmpty())
            ids << id;
    }
    if (ids.isEmpty()) {
        emit resultsReady(cached);
        return;
    }

    QPointer<BookmarksBridge> self(this);
    m_client.getByIds(ids, [self, cached, status](QVariantList items, QString errorMsg) {
        if (!self)
            return;

        if (!errorMsg.isEmpty()) {
            qWarning("BookmarksBridge: Shikimori %s", qUtf8Printable(errorMsg));
            emit self->error(errorMsg);
            emit self->resultsReady(cached);
            return;
        }

        QMap<QString, QVariantMap> byId;
        for (const QVariant &v : items) {
            const QVariantMap item = v.toMap();
            byId.insert(item.value("id").toString(), item);
        }

        QVariantList out;
        for (const QVariant &v : cached) {
            const QVariantMap row = v.toMap();
            const QString id = row.value("id").toString();
            QVariantMap merged = byId.value(id);
            if (merged.isEmpty()) {
                out << row;
                continue;
            }

            if (merged.value("title").toString().isEmpty())
                merged["title"] = row.value("title");

            const QString poster = ShikimoriClient::bestPosterUrl(
                merged.value("poster").toString(), merged.value("posterHd").toString());
            if (!poster.isEmpty()) {
                merged["poster"] = poster;
                merged["posterHd"] = merged.value("posterHd").toString().isEmpty()
                                         ? poster
                                         : merged.value("posterHd").toString();
                StatusStore::instance()->setStatus(
                    id, status, merged.value("title").toString(), poster);
            }

            out << merged;
        }

        emit self->resultsReady(out);
    });
}