#include "WelcomeScreenUI.h"
#include <QApplication>
#include <QPainter>
#include <QSettings>
#include <QListWidgetItem>
#include "icon_source.h"

WelcomeScreenUI::WelcomeScreenUI(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    loadRecentProjects();
}

WelcomeScreenUI::~WelcomeScreenUI()
{
}

void WelcomeScreenUI::setupUi()
{
    // Main layout with margins for VS Code-like appearance
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(100, 80, 100, 80);
    mainLayout->setSpacing(30);

    // Title
    m_titleLabel = new QLabel("SatExplorer");
    m_titleLabel->setStyleSheet("font-size: 48px; font-weight: bold; color: #4A9ACF;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);

    QLabel *subtitle = new QLabel("InSAR Data Processing & Visualization");
    subtitle->setStyleSheet("font-size: 18px; color: #666666;");
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);

    mainLayout->addSpacing(40);

    // Buttons container
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(40);
    btnLayout->setContentsMargins(100, 0, 100, 0);

    // New Project button
    m_newProjectBtn = new QPushButton("New Project");
    m_newProjectBtn->setMinimumSize(180, 50);
    m_newProjectBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #4A9ACF;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 5px;"
        "  font-size: 16px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #5AB9DF;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #3A8ABF;"
        "}"
    );
    connect(m_newProjectBtn, &QPushButton::clicked, this, &WelcomeScreenUI::newProjectRequested);
    btnLayout->addWidget(m_newProjectBtn);

    // Open Project button
    m_openProjectBtn = new QPushButton("Open Project");
    m_openProjectBtn->setMinimumSize(180, 50);
    m_openProjectBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #505050;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 5px;"
        "  font-size: 16px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #606060;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #404040;"
        "}"
    );
    connect(m_openProjectBtn, &QPushButton::clicked, this, &WelcomeScreenUI::openProjectRequested);
    btnLayout->addWidget(m_openProjectBtn);

    mainLayout->addLayout(btnLayout);
    mainLayout->addSpacing(40);

    // Recent projects
    m_recentLabel = new QLabel("Recent Projects");
    m_recentLabel->setStyleSheet("font-size: 14px; color: #666666; font-weight: bold;");
    mainLayout->addWidget(m_recentLabel);

    m_recentList = new QListWidget();
    m_recentList->setStyleSheet(
        "QListWidget {"
        "  background-color: rgba(255, 255, 255, 0.7);"
        "  border: 1px solid #dddddd;"
        "  border-radius: 5px;"
        "}"
        "QListWidget::item {"
        "  padding: 8px;"
        "  border-bottom: 1px solid #eeeeee;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: #f0f0f0;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #4A9ACF;"
        "  color: white;"
        "}"
    );
    m_recentList->setMaximumHeight(200);
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QString filePath = item->data(Qt::UserRole).toString();
        if (!filePath.isEmpty()) {
            emit recentProjectRequested(filePath);
        }
    });
    mainLayout->addWidget(m_recentList);

    setLayout(mainLayout);
}

void WelcomeScreenUI::loadRecentProjects()
{
    QSettings settings("Config.ini", QSettings::IniFormat);
    QStringList recent = settings.value("Recent/Projects", QStringList()).toStringList();

    m_recentList->clear();
    for (const QString &project : recent) {
        if (!project.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem(project);
            item->setData(Qt::UserRole, project);
            m_recentList->addItem(item);
        }
    }

    m_recentLabel->setVisible(m_recentList->count() > 0);
    m_recentList->setVisible(m_recentList->count() > 0);
}

void WelcomeScreenUI::refreshRecentProjects()
{
    loadRecentProjects();
}

QWidget* WelcomeScreenUI::centralWidget()
{
    return this;
}

QList<QToolBar*> WelcomeScreenUI::toolBars()
{
    // Welcome screen doesn't need its own toolbar
    return QList<QToolBar*>();
}

void WelcomeScreenUI::activate()
{
    show();
    refreshRecentProjects();
}

void WelcomeScreenUI::deactivate()
{
    hide();
}

QString WelcomeScreenUI::id() const
{
    return "welcome";
}

QString WelcomeScreenUI::displayName() const
{
    return "欢迎界面";
}

void WelcomeScreenUI::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    // Draw faded background BigIcon
    QPainter painter(this);

    // Detect theme from parent window
    bool isDarkTheme = false;
    QWidget* parent = parentWidget();
    while (parent) {
        QVariant bgColor = parent->property("theme-background");
        if (bgColor.isValid()) {
            QColor color = bgColor.value<QColor>();
            // 深色主题：背景色较深
            if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
                isDarkTheme = true;
            }
            break;
        }
        parent = parent->parentWidget();
    }

    // 根据主题设置不同的透明度，使图标成为淡淡的背景
    // 浅色主题：背景亮，图标稍微明显一点
    // 深色主题：背景深，图标需要更透明一点
    painter.setOpacity(isDarkTheme ? 0.025 : 0.04);

    // Load BigIcon as background
    QPixmap bgPixmap(BIGICON_BG);
    if (!bgPixmap.isNull()) {
        // Scale to reasonable size and center
        int size = qMin(width(), height()) * 0.7;
        QPixmap scaled = bgPixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    }

    painter.setOpacity(1.0);
}
