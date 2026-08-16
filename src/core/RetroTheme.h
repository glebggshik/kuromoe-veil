#pragma once

#include <QObject>

// Палитра ретро-темы (порт из retro-anime-app/app/globals.css) — тот же
// паттерн, что Theme.h: обычный QObject, регистрируется как QML-синглтон
// через qmlRegisterSingletonInstance в main.cpp (не через `pragma Singleton`
// в .qml — тот вариант давал гонку: RetroTheme.primary резолвился в
// undefined на самом первом кадре, когда синглтон использовался прямо в
// top-level биндингах root-компонента, загружаемого через loadFromModule).
class RetroTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString background READ background CONSTANT)
    Q_PROPERTY(QString foreground READ foreground CONSTANT)
    Q_PROPERTY(QString card READ card CONSTANT)
    Q_PROPERTY(QString cardForeground READ cardForeground CONSTANT)
    Q_PROPERTY(QString primary READ primary CONSTANT)
    Q_PROPERTY(QString primaryForeground READ primaryForeground CONSTANT)
    Q_PROPERTY(QString muted READ muted CONSTANT)
    Q_PROPERTY(QString mutedForeground READ mutedForeground CONSTANT)
    Q_PROPERTY(QString accent READ accent CONSTANT)
    Q_PROPERTY(QString accentForeground READ accentForeground CONSTANT)
    Q_PROPERTY(QString destructive READ destructive CONSTANT)
    Q_PROPERTY(QString border READ border CONSTANT)
    Q_PROPERTY(QString input READ input CONSTANT)
    Q_PROPERTY(QString sidebar READ sidebar CONSTANT)
    Q_PROPERTY(QString sidebarForeground READ sidebarForeground CONSTANT)
    Q_PROPERTY(QString sidebarAccent READ sidebarAccent CONSTANT)
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)

public:
    explicit RetroTheme(QObject *parent = nullptr) : QObject(parent) {}

    QString background() const { return "#060807"; }
    QString foreground() const { return "#cfe8d2"; }
    QString card() const { return "#0c0f0d"; }
    QString cardForeground() const { return "#cfe8d2"; }
    QString primary() const { return "#38f892"; }          // phosphor green
    QString primaryForeground() const { return "#05130a"; }
    QString muted() const { return "#10140f"; }
    QString mutedForeground() const { return "#6f8172"; }
    QString accent() const { return "#ffb32b"; }            // amber
    QString accentForeground() const { return "#1a1204"; }
    QString destructive() const { return "#ff5a4d"; }
    QString border() const { return "#1e2a20"; }
    QString input() const { return "#16201a"; }
    QString sidebar() const { return "#080a09"; }
    QString sidebarForeground() const { return "#8fa392"; }
    QString sidebarAccent() const { return "#10160f"; }
    int radius() const { return 0; }                        // sharp edges everywhere
    QString fontFamily() const { return "Consolas"; }
};
