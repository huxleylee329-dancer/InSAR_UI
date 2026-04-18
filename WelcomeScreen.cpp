#include "WelcomeScreen.h"
#include <QApplication>
#include <QPainter>
#include <QFileDialog>
#include <QListWidgetItem>
#include "icon_source.h"

WelcomeScreen::WelcomeScreen(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    loadRecentProjects();
}

WelcomeScreen::~WelcomeScreen()
{
}

void WelcomeScreen::setupUi()
{
    // Set window properties
    setWindowTitle("SatExplorer - Welcome");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(800, 600);
    resize(1000, 700);

    // Main layout
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
    connect(m_newProjectBtn, &QPushButton::clicked, this, &WelcomeScreen::onNewProjectClicked);
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
    connect(m_openProjectBtn, &QPushButton::clicked, this, &WelcomeScreen::onOpenProjectClicked);
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
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, &WelcomeScreen::onRecentItemDoubleClicked);
    mainLayout->addWidget(m_recentList);

    setLayout(mainLayout);
}

void WelcomeScreen::loadRecentProjects()
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

void WelcomeScreen::refreshRecentProjects()
{
    loadRecentProjects();
}

void WelcomeScreen::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);

    // Draw faded background logo
    QPainter painter(this);
    painter.setOpacity(0.05);

    // Load app icon as background
    QPixmap logoPixmap(APP_ICON);
    if (!logoPixmap.isNull()) {
        // Scale to reasonable size and center
        int size = qMin(width(), height()) * 0.6;
        QPixmap scaled = logoPixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    }

    painter.setOpacity(1.0);
}

void WelcomeScreen::onNewProjectClicked()
{
    emit newProjectRequested();
}

void WelcomeScreen::onOpenProjectClicked()
{
    emit openProjectRequested();
}

void WelcomeScreen::onRecentItemDoubleClicked(QListWidgetItem *item)
{
    QString filePath = item->data(Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit recentProjectRequested(filePath);
        accept();
    }
}
