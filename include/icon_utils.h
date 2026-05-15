#pragma once
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSize>

inline QIcon createColoredIcon(const QString &iconPath, const QColor &color)
{
    QIcon originalIcon(iconPath);
    QIcon coloredIcon;

    QList<QIcon::Mode> modes = { QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected };
    foreach (QIcon::Mode mode, modes) {
        QList<QIcon::State> states = { QIcon::Off, QIcon::On };
        foreach (QIcon::State state, states) {
            QPixmap pixmap = originalIcon.pixmap(QSize(24, 24), mode, state);
            if (!pixmap.isNull()) {
                QPixmap colored = pixmap;
                QPainter painter(&colored);
                painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                painter.fillRect(colored.rect(), color);
                painter.end();
                coloredIcon.addPixmap(colored, mode, state);
            }
        }
    }

    if (coloredIcon.isNull()) {
        QPixmap pixmap = originalIcon.pixmap(QSize(24, 24));
        if (!pixmap.isNull()) {
            QPixmap colored = pixmap;
            QPainter painter(&colored);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(colored.rect(), color);
            painter.end();
            coloredIcon = QIcon(colored);
        } else {
            coloredIcon = originalIcon;
        }
    }

    return coloredIcon;
}

inline QColor themeIconColor(bool isDark) {
    return isDark ? QColor(255, 255, 255) : QColor(0, 95, 172); // white / #005FAC
}
