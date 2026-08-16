#pragma once

#include <QObject>

// Палитра приложения — портирована 1:1 из gui/theme.py (Python-версия), чтобы
// старые QML-компоненты (Card.qml, PillButton.qml, HeroBanner.qml и т.п.),
// которые ссылаются на "Theme.xxx" как на глобальный объект, заработали без
// изменений. Регистрируется как context property "Theme" в main.cpp — не
// QML-синглтон, ровно как было в Python-версии (qt_bridge/theme.py).
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent CONSTANT)
    Q_PROPERTY(QString accentHover READ accentHover CONSTANT)
    Q_PROPERTY(QString accentLight READ accentLight CONSTANT)
    Q_PROPERTY(QString bgApp READ bgApp CONSTANT)
    Q_PROPERTY(QString bgSidebar READ bgSidebar CONSTANT)
    Q_PROPERTY(QString bgCard READ bgCard CONSTANT)
    Q_PROPERTY(QString bgCardHover READ bgCardHover CONSTANT)
    Q_PROPERTY(QString bgInput READ bgInput CONSTANT)
    Q_PROPERTY(QString bgPill READ bgPill CONSTANT)
    Q_PROPERTY(QString textPrimary READ textPrimary CONSTANT)
    Q_PROPERTY(QString textSecondary READ textSecondary CONSTANT)
    Q_PROPERTY(QString textMuted READ textMuted CONSTANT)
    Q_PROPERTY(QString good READ good CONSTANT)
    Q_PROPERTY(QString warn READ warn CONSTANT)
    Q_PROPERTY(QString bad READ bad CONSTANT)
    Q_PROPERTY(int corner READ corner CONSTANT)
    Q_PROPERTY(int cornerSmall READ cornerSmall CONSTANT)
    Q_PROPERTY(int cornerPill READ cornerPill CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr) : QObject(parent) {}

    QString accent() const { return "#a855f7"; }
    QString accentHover() const { return "#9333ea"; }
    QString accentLight() const { return "#c084fc"; }
    QString bgApp() const { return "#0a0a0f"; }
    QString bgSidebar() const { return "#111116"; }
    QString bgCard() const { return "#16161c"; }
    QString bgCardHover() const { return "#1f1f28"; }
    QString bgInput() const { return "#16161c"; }
    QString bgPill() const { return "#1c1c24"; }
    QString textPrimary() const { return "#f1f1f5"; }
    QString textSecondary() const { return "#a1a1aa"; }
    QString textMuted() const { return "#71717a"; }
    QString good() const { return "#22c55e"; }
    QString warn() const { return "#eab308"; }
    QString bad() const { return "#ef4444"; }
    int corner() const { return 16; }
    int cornerSmall() const { return 10; }
    int cornerPill() const { return 22; }
};
