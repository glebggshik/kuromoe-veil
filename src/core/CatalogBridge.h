#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "AniListClient.h"
#include "ShikimoriClient.h"

// Порт qt_bridge/catalog_bridge.py — тот же набор сигналов/слотов/свойств
// (имена сохранены 1:1: resultsReady, error, loading, page, hasNext, hasPrev,
// heroReady, randomReady, allGenres, loadPopular/loadOngoing/search/...),
// чтобы старый HomeView.qml/BrowseView.qml подключились без переписывания.
class CatalogBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int page READ page NOTIFY pageInfoChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY pageInfoChanged)
    Q_PROPERTY(bool hasPrev READ hasPrev NOTIFY pageInfoChanged)
    Q_PROPERTY(bool excludeChinese READ excludeChinese WRITE setExcludeChinese)

public:
    explicit CatalogBridge(QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    int page() const { return m_page; }
    bool hasNext() const { return m_hasNext; }
    bool hasPrev() const { return m_page > 1; }

    bool excludeChinese() const;
    void setExcludeChinese(bool value);

    Q_INVOKABLE QVariantList allGenres() const;

    Q_INVOKABLE void loadPopular();
    Q_INVOKABLE void loadOngoing();
    Q_INVOKABLE void loadLatest();
    // Только анонсы (status=anons), отдельно от loadLatest() (которая теперь
    // сама их исключает) — для секции "АНОНСЫ". Отдельный метод, а не общий
    // resultsReady от того же вызова: нужен отдельный экземпляр бриджа,
    // чтобы не перетереть список из loadLatest().
    Q_INVOKABLE void loadAnnounced();
    Q_INVOKABLE void search(const QString &title);
    Q_INVOKABLE void searchSuggestions(const QString &title);
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void prevPage();
    Q_INVOKABLE void applyFilters(const QString &kind, const QVariantList &genreIds, const QString &season);
    Q_INVOKABLE void pickRandom();
    Q_INVOKABLE void loadHero();
    Q_INVOKABLE void loadCalendar();

signals:
    void resultsReady(const QVariantList &items);
    void searchSuggestionsReady(const QVariantList &items);
    void error(const QString &message);
    void loadingChanged();
    void pageInfoChanged();
    void heroReady(const QVariant &item);
    void randomReady(const QVariant &item);
    void calendarReady(const QVariantList &days);
    void calendarLoadStarted();

private:
    enum class Mode { Browse, Search };

    void setLoading(bool value);
    void clearBrowseFilters();
    void fetch(int page);
    void onPageDone(const QVariantList &items, int page);
    void emitHeroWithBanners(QVariantMap item, const QVariantList &slides);

    ShikimoriClient m_client;
    AniListClient m_anilist;

    bool m_loading = false;
    int m_page = 1;
    bool m_hasNext = false;
    // Инкрементируется на каждый fetch() — при гонке из разных входных точек
    // (пагинация + смена фильтра/вкладки/поиска почти одновременно) не даёт
    // устаревшему ответу применить свою страницу поверх уже более новой.
    int m_fetchGen = 0;

    Mode m_mode = Mode::Browse;
    QString m_browseStatus;
    QString m_browseOrder = "popularity";
    QString m_searchTitle;

    QString m_filterKind;
    QVariantList m_filterGenreIds;
    QString m_filterSeason;
};
