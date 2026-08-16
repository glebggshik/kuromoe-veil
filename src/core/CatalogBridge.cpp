#include "CatalogBridge.h"

#include <QPointer>
#include <QRandomGenerator>

#include "AppConfig.h"
#include "HistoryManager.h"
#include "PosterCache.h"

namespace {

// Хиро — широкий баннер. Вертикальный постер (Shikimori/AniList coverImage) сюда
// подставлять нельзя: PreserveAspectCrop в широком прямоугольнике превращает портрет
// в нечитаемый увеличенный обрезок (жалоба пользователя — "глаз и кусок рамки вместо
// постера"). Если heroBanner нет — лучше плейсхолдер-градиент в QML, чем кривая картинка.
QString heroImageUrl(const QVariantMap &item) {
    auto usable = [](const QString &u) {
        return !u.isEmpty() && !u.contains(QStringLiteral("/missing_"));
    };
    const QString banner = item.value(QStringLiteral("heroBanner")).toString();
    if (usable(banner))
        return banner;
    return {};
}

QString heroCachedLocal(const QVariantMap &item) {
    auto tryCache = [](const QString &url) -> QString {
        if (url.isEmpty())
            return {};
        return PosterCache::instance()->cachedFile(url);
    };
    const QString banner = item.value(QStringLiteral("heroBanner")).toString();
    return tryCache(banner);
}

void preloadHeroImages(const QVariantMap &item) {
    auto queue = [](const QVariantMap &m) {
        const QString banner = m.value(QStringLiteral("heroBanner")).toString();
        if (!banner.isEmpty() && !banner.contains(QStringLiteral("/missing_")))
            PosterCache::instance()->requestPriority(banner);
        const QString url = heroImageUrl(m);
        if (!url.isEmpty() && url != banner)
            PosterCache::instance()->requestPriority(url);
    };
    queue(item);
    for (const QVariant &v : item.value(QStringLiteral("heroSlides")).toList())
        queue(v.toMap());
}

void emitHeroWhenImageReady(CatalogBridge *bridge, QVariantMap out) {
    QPointer<CatalogBridge> self(bridge);
    if (!self)
        return;

    const QString remote = heroImageUrl(out);
    if (remote.isEmpty()) {
        qInfo("CatalogBridge: hero '%s' (no image url)", qUtf8Printable(out.value(QStringLiteral("title")).toString()));
        emit self->heroReady(out);
        return;
    }

    // Только уже готовый кэш (баннер или постер) — без блокирующей загрузки.
    const QString cached = heroCachedLocal(out);
    if (!cached.isEmpty())
        out[QStringLiteral("heroImageLocal")] = cached;

    // "Продолжить: ..." — запоминаем URL баннера (не дожидаясь, пока файл
    // реально появится на диске: если требовать !cached.isEmpty() здесь, то
    // при переключении на только что открытый тайтл — когда баннер ещё
    // качается в фоне — снимок не обновлялся бы, и при следующем запуске
    // показывалась бы обложка ПРЕДЫДУЩЕГО тайтла). К следующему старту файл
    // почти наверняка уже будет в PosterCache (см. DetailBridge::load
    // проактивное кэширование), а HeroBanner.qml сам резолвит cachedFile()
    // живьём при показе.
    if (out.value(QStringLiteral("continuing")).toBool()
        && !out.value(QStringLiteral("heroBanner")).toString().isEmpty())
        AppConfig::instance()->setLastHero(out);

    preloadHeroImages(out);

    const bool hasBanner = !out.value(QStringLiteral("heroBanner")).toString().isEmpty();
    qInfo(
        "CatalogBridge: hero '%s' banner=%s (immediate, cache async)",
        qUtf8Printable(out.value(QStringLiteral("title")).toString()),
        qUtf8Printable(hasBanner ? "yes" : "no"));
    emit self->heroReady(out);
}

} // namespace

CatalogBridge::CatalogBridge(QObject *parent) : QObject(parent) {
    connect(AppConfig::instance(), &AppConfig::excludeChineseChanged, this, [this]() {
        if (m_page >= 1)
            fetch(m_page);
        loadCalendar();
        loadHero();
    });
}

bool CatalogBridge::excludeChinese() const { return AppConfig::instance()->excludeChinese(); }

void CatalogBridge::setExcludeChinese(bool value) {
    AppConfig::instance()->setExcludeChinese(value);
    fetch(m_page);
}

QVariantList CatalogBridge::allGenres() const { return ShikimoriClient::allGenres(); }

void CatalogBridge::setLoading(bool value) {
    if (value == m_loading)
        return;
    m_loading = value;
    emit loadingChanged();
}

void CatalogBridge::fetch(int page) {
    setLoading(true);
    const int gen = ++m_fetchGen;

    QPointer<CatalogBridge> self(this);
    auto onDone = [self, page, gen](QVariantList items, QString errorMsg) {
        if (!self || gen != self->m_fetchGen)
            return;
        self->setLoading(false);
        if (!errorMsg.isEmpty()) {
            qWarning("CatalogBridge: fetch failed — %s", qUtf8Printable(errorMsg));
            emit self->error(errorMsg);
            return;
        }
        self->onPageDone(items, page);
    };

    if (m_mode == Mode::Search) {
        m_client.search(
            m_searchTitle, 52, page, excludeChinese(),
            m_filterKind, m_filterGenreIds, m_filterSeason, onDone);
    } else {
        m_client.browse(
            m_browseStatus, m_filterKind, m_filterGenreIds, m_browseOrder,
            page, 52, excludeChinese(), m_filterSeason, onDone);
    }
}

void CatalogBridge::onPageDone(const QVariantList &items, int page) {
    m_page = page;
    m_hasNext = !items.isEmpty();
    qInfo("CatalogBridge: page %d ready, %d items", page, items.size());
    emit pageInfoChanged();
    emit resultsReady(items);
}

void CatalogBridge::clearBrowseFilters() {
    m_filterKind.clear();
    m_filterGenreIds.clear();
    m_filterSeason.clear();
}

void CatalogBridge::loadPopular() {
    qInfo("CatalogBridge::loadPopular");
    m_mode = Mode::Browse;
    m_browseStatus.clear();
    m_browseOrder = "popularity";
    clearBrowseFilters();
    fetch(1);
}

void CatalogBridge::loadOngoing() {
    qInfo("CatalogBridge::loadOngoing");
    m_mode = Mode::Browse;
    m_browseStatus = QStringLiteral("ongoing");
    m_browseOrder = "popularity";
    clearBrowseFilters();
    fetch(1);
}

void CatalogBridge::loadLatest() {
    qInfo("CatalogBridge::loadLatest");
    m_mode = Mode::Browse;
    // order=aired_on без фильтра статуса сортирует по ДАТЕ ВЫХОДА, а не по
    // тому, вышло ли уже что-то — анонсы с датой в 2027-2028 обгоняют
    // реально вышедшее и вытесняют его со страницы 1 почти полностью.
    // "released,ongoing" — то, что уже реально выходит/вышло; Shikimori
    // принимает status списком через запятую.
    m_browseStatus = QStringLiteral("released,ongoing");
    m_browseOrder = "aired_on";
    clearBrowseFilters();
    fetch(1);
}

void CatalogBridge::loadAnnounced() {
    qInfo("CatalogBridge::loadAnnounced");
    m_mode = Mode::Browse;
    m_browseStatus = QStringLiteral("anons");
    m_browseOrder = "aired_on";
    clearBrowseFilters();
    fetch(1);
}

void CatalogBridge::search(const QString &title) {
    QString trimmed = title.trimmed();
    if (trimmed.isEmpty())
        return;
    m_mode = Mode::Search;
    m_searchTitle = trimmed;
    fetch(1);
}

void CatalogBridge::searchSuggestions(const QString &title) {
    const QString trimmed = title.trimmed();
    if (trimmed.size() < 2) {
        emit searchSuggestionsReady({});
        return;
    }

    QPointer<CatalogBridge> self(this);
    m_client.search(
        trimmed, 3, 1, excludeChinese(), QString(), {}, QString(),
        [self](QVariantList items, QString errorMsg) {
            if (!self)
                return;
            if (!errorMsg.isEmpty())
                emit self->searchSuggestionsReady({});
            else
                emit self->searchSuggestionsReady(items);
        });
}

void CatalogBridge::nextPage() {
    // Пока предыдущая страница ещё грузится — игнорируем повторные клики.
    // Без этого частый спам по "вперёд" запускал несколько параллельных
    // fetch() без общего generation-токена: ответы приходили не по порядку,
    // а сетка карточек (со своей асинхронной загрузкой постеров) успевала
    // несколько раз пересобраться внахлёст — один из источников крэшей
    // 0xC0000005 в Qt6Cored.dll.
    if (m_loading)
        return;
    if (m_hasNext)
        fetch(m_page + 1);
}

void CatalogBridge::prevPage() {
    if (m_loading)
        return;
    if (m_page > 1)
        fetch(m_page - 1);
}

void CatalogBridge::applyFilters(const QString &kind, const QVariantList &genreIds, const QString &season) {
    m_mode = Mode::Browse;
    m_browseStatus.clear();
    m_browseOrder = QStringLiteral("popularity");
    m_filterKind = kind;
    m_filterGenreIds = genreIds;
    m_filterSeason = season.trimmed();
    fetch(1);
}

void CatalogBridge::pickRandom() {
    setLoading(true);
    int randomPage = QRandomGenerator::global()->bounded(1, 31); // 1..30, как в Python-версии
    QPointer<CatalogBridge> self(this);
    m_client.browse(
        "", "", {}, "ranked", randomPage, 30, excludeChinese(), "",
        [self](QVariantList items, QString errorMsg) {
            if (!self)
                return;
            self->setLoading(false);
            if (!errorMsg.isEmpty()) {
                emit self->error(errorMsg);
                return;
            }
            if (items.isEmpty()) {
                emit self->randomReady(QVariant());
                return;
            }
            int idx = QRandomGenerator::global()->bounded(items.size());
            emit self->randomReady(items[idx]);
        });
}

void CatalogBridge::loadCalendar() {
    qInfo("CatalogBridge::loadCalendar");
    emit calendarLoadStarted();
    QPointer<CatalogBridge> self(this);
    m_client.getCalendar(excludeChinese(), [self](QVariantList days, QString errorMsg) {
        if (!self)
            return;
        if (!errorMsg.isEmpty()) {
            emit self->error(errorMsg);
            return;
        }
        emit self->calendarReady(days);
    });
}

void CatalogBridge::emitHeroWithBanners(QVariantMap item, const QVariantList &slides) {
    if (item.isEmpty()) {
        emit heroReady(QVariant());
        return;
    }

    const QVariantList source = slides.isEmpty() ? QVariantList{item} : slides;
    QPointer<CatalogBridge> self(this);
    m_anilist.enrichHeroBanners(source, [self, item](QVariantList enriched) {
        if (!self)
            return;
        QVariantList withBanner;
        for (const QVariant &v : enriched) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("heroBanner")).toString().isEmpty())
                withBanner << m;
        }

        QVariantMap out = item;
        if (!withBanner.isEmpty()) {
            out = withBanner.first().toMap();
            out["continuing"] = item.value(QStringLiteral("continuing"));
            out["lastEpisode"] = item.value(QStringLiteral("lastEpisode"));
            if (!item.value(QStringLiteral("description")).toString().isEmpty())
                out["description"] = item.value(QStringLiteral("description"));
            if (withBanner.size() > 1 && !out.value(QStringLiteral("continuing")).toBool())
                out[QStringLiteral("heroSlides")] = withBanner;
            else
                out.remove(QStringLiteral("heroSlides"));
        } else if (!enriched.isEmpty()) {
            // AниList не дал bannerImage для этого тайтла — вертикальный постер сюда
            // не годится (см. heroImageUrl), оставляем item как есть без heroBanner,
            // QML покажет плейсхолдер вместо обрезанной вертикальной картинки.
            out.remove(QStringLiteral("heroSlides"));
        } else {
            out.remove(QStringLiteral("heroSlides"));
        }
        emitHeroWhenImageReady(self.data(), out);
    });
}

void CatalogBridge::loadHero() {
    qInfo("CatalogBridge::loadHero");
    HistoryManager *history = HistoryManager::instance();

    QString titleId;
    int lastEpisode = 1;
    if (!history->activeTitleId().isEmpty()) {
        titleId = history->activeTitleId();
        lastEpisode = history->currentEpisode();
    } else {
        const QVariantMap recent = history->mostRecent();
        if (!recent.value("found").toBool())
            titleId.clear();
        else {
            titleId = recent.value("titleId").toString();
            lastEpisode = recent.value("episode").toInt();
        }
    }

    QPointer<CatalogBridge> self(this);
    if (!titleId.isEmpty()) {
        m_client.getDetails(titleId, [self, lastEpisode](QVariantMap item, QString errorMsg) {
            if (!self)
                return;
            if (!errorMsg.isEmpty() || item.isEmpty()) {
                emit self->heroReady(QVariant());
                return;
            }
            item[QStringLiteral("continuing")] = true;
            item[QStringLiteral("lastEpisode")] = lastEpisode;
            self->emitHeroWithBanners(item, {item});
        });
        return;
    }

    m_client.browse(
        "", "", {}, "popularity", 1, 8, excludeChinese(), "",
        [self](QVariantList items, QString errorMsg) {
            if (!self)
                return;
            if (!errorMsg.isEmpty() || items.isEmpty()) {
                emit self->heroReady(QVariant());
                return;
            }
            QVariantMap item = items.first().toMap();
            item[QStringLiteral("continuing")] = false;
            item[QStringLiteral("lastEpisode")] = 0;
            self->emitHeroWithBanners(item, items);
        });
}
