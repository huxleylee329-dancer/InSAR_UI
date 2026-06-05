#pragma once
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSize>
#include <QHash>
#include <QToolBar>
#include <QToolButton>
#include <functional>

inline QIcon createColoredIcon(const QString &iconPath, const QColor &color, const QColor &selectedColor = QColor())
{
    // Cache: same (path, color, selectedColor) returns the same QIcon
    static QHash<QString, QIcon> cache;
    QString key = iconPath + "|" + color.name() + "|" + selectedColor.name();
    if (cache.contains(key)) return cache.value(key);

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
                
                QColor drawColor = color;
                if ((mode == QIcon::Selected || mode == QIcon::Active) && selectedColor.isValid()) {
                    drawColor = selectedColor;
                }
                
                painter.fillRect(colored.rect(), drawColor);
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
            
            if (selectedColor.isValid()) {
                QPixmap selectedPix = pixmap;
                QPainter painterSel(&selectedPix);
                painterSel.setCompositionMode(QPainter::CompositionMode_SourceIn);
                painterSel.fillRect(selectedPix.rect(), selectedColor);
                painterSel.end();
                coloredIcon.addPixmap(selectedPix, QIcon::Selected);
                coloredIcon.addPixmap(selectedPix, QIcon::Active);
            }
        } else {
            coloredIcon = originalIcon;
        }
    }

    cache.insert(key, coloredIcon);
    return coloredIcon;
}

inline QColor themeIconColor(bool isDark) {
    return isDark ? QColor(255, 255, 255) : QColor(0, 95, 172); // white / #005FAC
}

inline QToolButton* createToolbarButton(const QString &iconPath, const QString &text,
                                         const QColor &iconColor = QColor("#414752"),
                                         const QColor &textColor = QColor("#595F66"),
                                         QWidget *parent = nullptr)
{
    QToolButton *btn = new QToolButton(parent);
    btn->setIcon(createColoredIcon(iconPath, iconColor));
    btn->setIconSize(QSize(24, 24));
    btn->setText(" " + text);
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setProperty("iconPath", iconPath);

    QString textColorHex = textColor.name();
    btn->setStyleSheet(
        QString("QToolButton { "
        "  border: none; "
        "  border-radius: 4px; "
        "  background-color: transparent; "
        "  color: %1; "
        "  font-size: 11px; "
        "  font-weight: bold; "
        "  text-transform: uppercase; "
        "  letter-spacing: 0.5px; "
        "  padding: 0px 6px; "
        "  margin: 0px; "
        "  min-height: 20px; "
        "  max-height: 20px; "
        "}"
        "QToolButton:hover { "
        "  background-color: #E0E0E0; "
        "}"
        "QToolButton:pressed { "
        "  background-color: #D0D0D0; "
        "}").arg(textColorHex)
    );
    return btn;
}

// iconColorForButton(btnText, isDark) returns the icon color for a given button
inline void applyToolbarTheme(QToolBar* toolbar, const QString& theme,
                              std::function<QColor(const QString& btnText, bool isDark)> iconColorForButton)
{
    if (!toolbar) return;

    bool isDark = (theme == "dark");
    QString toolbarStyle;
    QColor separatorColor;
    QColor textColor = isDark ? QColor("#c1c7cf") : QColor("#595F66");

    if (isDark) {
        toolbarStyle = R"(
            QToolBar {
                background-color: #1a1c1c;
                border-bottom: 1px solid rgba(135, 141, 152, 0.3);
            }
        )";
        separatorColor = QColor(135, 141, 152, 77);
    } else if (theme == "light") {
        toolbarStyle = R"(
            QToolBar {
                background-color: #f3f3f3;
                border-bottom: 1px solid rgba(192, 199, 212, 0.3);
            }
        )";
        separatorColor = QColor(192, 199, 212, 77);
    } else { // fusion
        toolbarStyle = R"(
            QToolBar {
                background-color: #f0f0f0;
                border-bottom: 1px solid rgba(74, 154, 207, 0.3);
            }
        )";
        separatorColor = QColor(74, 154, 207, 77);
    }

    toolbar->setStyleSheet(toolbarStyle);
    toolbar->setContentsMargins(0, 0, 0, 0);

    // Update separators and buttons in a single pass
    for (QObject *obj : toolbar->children()) {
        QWidget *widget = qobject_cast<QWidget*>(obj);
        if (!widget) continue;

        // Separator: plain QWidget with no object name
        if (widget->metaObject()->className() == QString("QWidget")) {
            widget->setStyleSheet(QString("background-color: %1; margin: 2px 0px;").arg(separatorColor.name(QColor::HexArgb)));
            continue;
        }

        // Toolbar button
        QToolButton *btn = qobject_cast<QToolButton*>(widget);
        if (!btn) continue;

        QString hoverBg = isDark ? "#2f3131" : "#E0E0E0";
        QString pressedBg = isDark ? "#3f4141" : "#D0D0D0";
        btn->setStyleSheet(
            QString("QToolButton { "
            "  border: none; "
            "  border-radius: 4px; "
            "  background-color: transparent; "
            "  color: %1; "
            "  font-size: 11px; "
            "  font-weight: bold; "
            "  text-transform: uppercase; "
            "  letter-spacing: 0.5px; "
            "  padding: 0px 6px; "
            "  margin: 0px; "
            "  min-height: 20px; "
            "  max-height: 20px; "
            "}"
            "QToolButton:hover { "
            "  background-color: %2; "
            "}"
            "QToolButton:pressed { "
            "  background-color: %3; "
            "}").arg(textColor.name(), hoverBg, pressedBg)
        );

        QString iconPath = btn->property("iconPath").toString();
        if (!iconPath.isEmpty()) {
            QString btnText = btn->text().trimmed();
            QColor c = iconColorForButton(btnText, isDark);
            btn->setIcon(createColoredIcon(iconPath, c));
        }
    }
}
