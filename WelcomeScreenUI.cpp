#include "WelcomeScreenUI.h"
#include <QApplication>
#include <QPainter>
#include <QSettings>
#include <QListWidgetItem>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QScrollArea>
#include <QImage>
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
    mainLayout->setContentsMargins(80, 60, 80, 60);
    mainLayout->setSpacing(40);

    // Title
    m_titleLabel = new QLabel("SatExplorer");

    m_titleLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: #005FAC;");
    m_titleLabel->setAlignment(Qt::AlignLeft);
    mainLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(QStringLiteral("InSAR 数据处理与可视化"));
    m_subtitleLabel->setStyleSheet("font-size: 14px; color: #888888;");
    m_subtitleLabel->setAlignment(Qt::AlignLeft);
    mainLayout->addWidget(m_subtitleLabel);

    mainLayout->addSpacing(20);

    // Main grid layout
    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->setColumnStretch(0, 5);  // Quick Start - 5 columns
    gridLayout->setColumnStretch(1, 7);  // Recent Projects - 7 columns
    gridLayout->setHorizontalSpacing(40);
    gridLayout->setVerticalSpacing(40);

    // Left column: Quick Start
    QWidget *quickStartContainer = new QWidget();
    QVBoxLayout *quickStartLayout = new QVBoxLayout(quickStartContainer);
    quickStartLayout->setContentsMargins(0, 0, 0, 0);
    quickStartLayout->setSpacing(12);

    QLabel *quickStartLabel = new QLabel(QStringLiteral("快速开始"));
    quickStartLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #0078d7; letter-spacing: 1px; text-transform: uppercase;");
    quickStartLayout->addWidget(quickStartLabel);

    quickStartLayout->addSpacing(10);

    // New Project button with icon and description
    m_newProjectBtn = qobject_cast<QPushButton*>(createQuickStartButton("add_circle", QStringLiteral("新建项目"), QStringLiteral("创建新项目")));
    connect(m_newProjectBtn, &QPushButton::clicked, this, &WelcomeScreenUI::newProjectRequested);
    quickStartLayout->addWidget(m_newProjectBtn);

    // Open Project button
    m_openProjectBtn = qobject_cast<QPushButton*>(createQuickStartButton("folder_open", QStringLiteral("打开项目"), QStringLiteral("从磁盘加载现有的 .insar 工作区")));
    connect(m_openProjectBtn, &QPushButton::clicked, this, &WelcomeScreenUI::openProjectRequested);
    quickStartLayout->addWidget(m_openProjectBtn);

    // Fetch Data button
    m_fetchDataBtn = qobject_cast<QPushButton*>(createQuickStartButton("cloud_download", QStringLiteral("获取数据"), QStringLiteral("连接云端服务器检索数据")));
    connect(m_fetchDataBtn, &QPushButton::clicked, this, &WelcomeScreenUI::fetchDataRequested);
    quickStartLayout->addWidget(m_fetchDataBtn);

    quickStartLayout->addStretch();

    gridLayout->addWidget(quickStartContainer, 0, 0);

    // Right column: Recent Projects
    QWidget *recentContainer = new QWidget();
    QVBoxLayout *recentLayout = new QVBoxLayout(recentContainer);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(12);

    // Header with "Recent Projects" label only (removed "View All")
    QHBoxLayout *recentHeaderLayout = new QHBoxLayout();
    QLabel *recentLabel = new QLabel(QStringLiteral("最近项目"));
    recentLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #0078d7; letter-spacing: 1px; text-transform: uppercase;");
    recentHeaderLayout->addWidget(recentLabel);
    recentHeaderLayout->addStretch();

    recentLayout->addLayout(recentHeaderLayout);

    recentLayout->addSpacing(10);

    // Scroll Area for Recent Projects list to handle up to 10 projects beautifully
    m_recentScrollArea = new QScrollArea();
    m_recentScrollArea->setWidgetResizable(true);
    m_recentScrollArea->setFrameShape(QFrame::NoFrame);
    m_recentScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_recentScrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    // Recent projects list container
    m_recentProjectsContainer = new QWidget();
    m_recentProjectsContainer->setStyleSheet("background: transparent;");
    QVBoxLayout *recentListLayout = new QVBoxLayout(m_recentProjectsContainer);
    recentListLayout->setContentsMargins(0, 0, 0, 0);
    recentListLayout->setSpacing(0);
    
    m_recentScrollArea->setWidget(m_recentProjectsContainer);
    recentLayout->addWidget(m_recentScrollArea);

    gridLayout->addWidget(recentContainer, 0, 1);

    mainLayout->addLayout(gridLayout);

    // Tip of the Day - Bottom section
    m_tipFrame = new QFrame();
    m_tipFrame->setObjectName("tipFrame");
    m_tipFrame->setStyleSheet(
        "#tipFrame {"
        "  background-color: rgba(0, 120, 215, 0.04);"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 12px;"
        "}"
    );

    QHBoxLayout *tipLayout = new QHBoxLayout(m_tipFrame);
    tipLayout->setContentsMargins(12, 12, 12, 12);
    tipLayout->setSpacing(12);

    // Light bulb icon (using Unicode)
    QLabel *tipIconLabel = new QLabel(u8"\U0001F4A1");
    tipIconLabel->setStyleSheet("font-size: 20px; border: none;");
    tipIconLabel->setFixedSize(48, 48);
    tipIconLabel->setAlignment(Qt::AlignCenter);
    tipLayout->addWidget(tipIconLabel);

    QWidget *tipContent = new QWidget();
    tipContent->setStyleSheet("border: none;");
    QVBoxLayout *tipContentLayout = new QVBoxLayout(tipContent);
    tipContentLayout->setContentsMargins(0, 0, 0, 0);
    tipContentLayout->setSpacing(4);

    m_tipTitleLabel = new QLabel(QStringLiteral("每日提示"));
    m_tipTitleLabel->setStyleSheet("font-size: 10px; font-weight: bold; color: #994700; letter-spacing: 1px; text-transform: uppercase;");
    tipContentLayout->addWidget(m_tipTitleLabel);

    m_tipTextLabel = new QLabel(QStringLiteral("在处理大型数据栈时，提早使用多视处理（Multi-Look）能有效降低相位噪声并加速干涉图的生成。您可以在“分析属性”面板中调整视数。"));
    m_tipTextLabel->setStyleSheet("font-size: 13px; color: #AAAAAA;");
    m_tipTextLabel->setWordWrap(true);
    tipContentLayout->addWidget(m_tipTextLabel);

    tipLayout->addWidget(tipContent);

    mainLayout->addWidget(m_tipFrame);

    setLayout(mainLayout);
}

void WelcomeScreenUI::loadRecentProjects()
{
    QSettings settings("Config.ini", QSettings::IniFormat);
    QStringList recent = settings.value("Recent/Projects", QStringList()).toStringList();

    // Clear existing recent projects
    QLayoutItem *child;
    while ((child = m_recentProjectsContainer->layout()->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    for (int i = 0; i < recent.size() && i < 10; ++i) {
        const QString &project = recent[i];
        if (!project.isEmpty()) {
            QFileInfo fileInfo(project);
            QString name = fileInfo.baseName();
            QString path = fileInfo.path();
            
            // Format actual modification time if file exists, else show file not found
            QString time = "";
            if (fileInfo.exists()) {
                time = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm");
            } else {
                time = QStringLiteral("文件未找到");
            }

            QWidget *item = createRecentProjectItem(name, path, time);
            item->setProperty("filePath", project);
            item->installEventFilter(this);
            m_recentProjectsContainer->layout()->addWidget(item);
        }
    }

    // Add a stretch at the end to keep items aligned to the top of the scroll container
    if (QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(m_recentProjectsContainer->layout())) {
        layout->addStretch();
    }
}

void WelcomeScreenUI::refreshRecentProjects()
{
    loadRecentProjects();
    updateThemeStyles();
}

QWidget* WelcomeScreenUI::createQuickStartButton(const QString &iconName, const QString &title, const QString &description)
{
    QPushButton *btn = new QPushButton();
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn->setFixedHeight(70);
    btn->setStyleSheet(
        "QPushButton {"
        "  background-color: #2B2B2B;"
        "  border: none;"
        "  border-left: 2px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 16px;"
        "  text-align: left;"
        "}"
        "QPushButton:hover {"
        "  background-color: #404040;"
        "  border-left: 2px solid #0078d7;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #404040;"
        "}"
    );

    QHBoxLayout *btnLayout = new QHBoxLayout(btn);
    btnLayout->setContentsMargins(16, 0, 0, 0);
    btnLayout->setSpacing(16);

    // Icon container
    QLabel *iconLabel = new QLabel();
    iconLabel->setStyleSheet(
        "background-color: #404040;"
        "  border-radius: 4px;"
    );
    iconLabel->setFixedSize(40, 40);
    iconLabel->setAlignment(Qt::AlignCenter);

    // Use emoji icons for now
    QString iconText;
    if (iconName == "add_circle") iconText = u8"\u2795";
    else if (iconName == "folder_open") iconText = u8"\U0001F4C1";
    else if (iconName == "cloud_download") iconText = u8"\u2601";

    iconLabel->setText(iconText);
    iconLabel->setStyleSheet(
        "background-color: #404040;"
        "  border-radius: 4px;"
        "  font-size: 24px;"
    );
    btnLayout->addWidget(iconLabel);

    // Text container
    QWidget *textContainer = new QWidget();
    QVBoxLayout *textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);

    QLabel *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #FFFFFF;");
    textLayout->addWidget(titleLabel);

    QLabel *descLabel = new QLabel(description);
    descLabel->setStyleSheet("font-size: 12px; color: #888888;");
    textLayout->addWidget(descLabel);

    btnLayout->addWidget(textContainer);
    btnLayout->addStretch();

    return btn;
}

QWidget* WelcomeScreenUI::createRecentProjectItem(const QString &name, const QString &path, const QString &time)
{
    QWidget *item = new QWidget();
    item->setStyleSheet(
        "QWidget:hover {"
        "  background-color: #404040;"
        "  border-radius: 4px;"
        "}"
    );
    item->setCursor(Qt::PointingHandCursor);
    item->setFixedHeight(50);

    QHBoxLayout *itemLayout = new QHBoxLayout(item);
    itemLayout->setContentsMargins(8, 0, 12, 0);
    itemLayout->setSpacing(12);

    // Document icon
    QLabel *iconLabel = new QLabel(u8"\U0001F4C4");
    iconLabel->setStyleSheet("font-size: 18px; color: #666666;");
    itemLayout->addWidget(iconLabel);

    // Text container
    QWidget *textContainer = new QWidget();
    QVBoxLayout *textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QLabel *nameLabel = new QLabel(name);
    nameLabel->setStyleSheet("font-size: 14px; font-weight: 500; color: #f9f9f9;");
    textLayout->addWidget(nameLabel);

    QLabel *pathLabel = new QLabel(path);
    pathLabel->setStyleSheet("font-size: 10px; color: #666666; font-family: monospace;");
    textLayout->addWidget(pathLabel);

    itemLayout->addWidget(textContainer);

    itemLayout->addStretch();

    // Time label
    if (!time.isEmpty()) {
        QLabel *timeLabel = new QLabel(time);
        timeLabel->setStyleSheet("font-size: 10px; color: #666666;");
        itemLayout->addWidget(timeLabel);
    }

    // Bottom border (using a QFrame)
    QFrame *separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("QFrame { background-color: #404040; }");

    return item;
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
    updateThemeStyles();
}

void WelcomeScreenUI::updateThemeStyles()
{
    // Detect theme from own property (consistent with other components)
    bool isDarkTheme = false;
    QVariant bgColor = property("theme-background");
    if (bgColor.isValid()) {
        QColor color = bgColor.value<QColor>();
        if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
            isDarkTheme = true;
        }
    }
    m_isDarkTheme = isDarkTheme;

    // Define colors based on theme (from UI.md spec)
    const char *bgColorStr = isDarkTheme ? "#2B2B2B" : "#F0F0F0";  // Dock background
    const char *textColor = isDarkTheme ? "#FFFFFF" : "#333333";   // Text color
    const char *secondaryText = isDarkTheme ? "#888888" : "#666666";  // Secondary text
    const char *btnBg = isDarkTheme ? "#2B2B2B" : "#FFFFFF";     // Button background
    const char *btnHoverBg = isDarkTheme ? "#404040" : "#E8E8E8";  // Button hover
    const char *btnBorder = isDarkTheme ? "transparent" : "#dddddd";
    const char *iconBg = isDarkTheme ? "#404040" : "#F0F0F0";   // Icon container background
    const char *itemHoverBg = isDarkTheme ? "#404040" : "#E8E8E8";  // Item hover background
    const char *borderColor = isDarkTheme ? "#404040" : "#E0E0E0";   // Border color
    const char *tipBg = isDarkTheme ? "rgba(0, 120, 215, 0.04)" : "rgba(0, 120, 215, 0.02)";
    const char *tipColor = isDarkTheme ? "#0078d7" : "#005FAC";

    // Update title and subtitle
    m_titleLabel->setStyleSheet(QString("font-size: 36px; font-weight: bold; color: #005FAC;"));
    m_subtitleLabel->setStyleSheet(QString("font-size: 14px; color: %1;").arg(secondaryText));

    // Update quick start buttons
    if (m_newProjectBtn) {
        m_newProjectBtn->setStyleSheet(QString(
            "QPushButton {"
            "  background-color: %1;"
            "  border: none;"
            "  border-left: 2px solid transparent;"
            "  border-radius: 4px;"
            "  padding: 16px;"
            "  text-align: left;"
            "}"
            "QPushButton:hover {"
            "  background-color: %2;"
            "  border-left: 2px solid #0078d7;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #333333;"
            "}"
        ).arg(btnBg, btnHoverBg));

        // Update child labels
        QList<QLabel*> labels = m_newProjectBtn->findChildren<QLabel*>();
        if (labels.size() >= 3) {
            labels[0]->setStyleSheet(QString("background-color: %1; border-radius: 4px; font-size: 24px;").arg(iconBg));
            labels[1]->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(textColor));
            labels[2]->setStyleSheet(QString("font-size: 12px; color: %1;").arg(secondaryText));
        }
    }

    if (m_openProjectBtn) {
        m_openProjectBtn->setStyleSheet(QString(
            "QPushButton {"
            "  background-color: %1;"
            "  border: none;"
            "  border-left: 2px solid transparent;"
            "  border-radius: 4px;"
            "  padding: 16px;"
            "  text-align: left;"
            "}"
            "QPushButton:hover {"
            "  background-color: %2;"
            "  border-left: 2px solid #0078d7;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #333333;"
            "}"
        ).arg(btnBg, btnHoverBg));

        QList<QLabel*> labels = m_openProjectBtn->findChildren<QLabel*>();
        if (labels.size() >= 3) {
            labels[0]->setStyleSheet(QString("background-color: %1; border-radius: 4px; font-size: 24px;").arg(iconBg));
            labels[1]->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(textColor));
            labels[2]->setStyleSheet(QString("font-size: 12px; color: %1;").arg(secondaryText));
        }
    }

    if (m_fetchDataBtn) {
        m_fetchDataBtn->setStyleSheet(QString(
            "QPushButton {"
            "  background-color: %1;"
            "  border: none;"
            "  border-left: 2px solid transparent;"
            "  border-radius: 4px;"
            "  padding: 16px;"
            "  text-align: left;"
            "}"
            "QPushButton:hover {"
            "  background-color: %2;"
            "  border-left: 2px solid #0078d7;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #333333;"
            "}"
        ).arg(btnBg, btnHoverBg));

        QList<QLabel*> labels = m_fetchDataBtn->findChildren<QLabel*>();
        if (labels.size() >= 3) {
            labels[0]->setStyleSheet(QString("background-color: %1; border-radius: 4px; font-size: 24px;").arg(iconBg));
            labels[1]->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(textColor));
            labels[2]->setStyleSheet(QString("font-size: 12px; color: %1;").arg(secondaryText));
        }
    }

    // Update scroll area styles (scrollbar aesthetics)
    if (m_recentScrollArea) {
        m_recentScrollArea->setStyleSheet(QString(
            "QScrollArea {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "QScrollBar:vertical {"
            "  width: 8px;"
            "  background: transparent;"
            "  margin: 0px 0px 0px 0px;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: %1;"
            "  min-height: 20px;"
            "  border-radius: 4px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            "  background: %2;"
            "}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
            "  height: 0px;"
            "  background: none;"
            "}"
        ).arg(isDarkTheme ? "#555555" : "#cccccc", isDarkTheme ? "#888888" : "#999999"));
    }

    // Update recent project items
    if (m_recentProjectsContainer) {
        QList<QWidget*> items = m_recentProjectsContainer->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* item : items) {
            item->setStyleSheet(QString(
                "QWidget:hover {"
                "  background-color: %1;"
                "  border-radius: 4px;"
                "}"
            ).arg(itemHoverBg));

            QList<QLabel*> labels = item->findChildren<QLabel*>();
            if (labels.size() >= 4) {
                labels[0]->setStyleSheet(QString("font-size: 18px; color: %1;").arg(secondaryText));
                labels[1]->setStyleSheet(QString("font-size: 14px; font-weight: 500; color: %1;").arg(textColor));
                labels[2]->setStyleSheet(QString("font-size: 10px; color: %1; font-family: monospace;").arg(secondaryText));
                if (labels[3]) {
                    labels[3]->setStyleSheet(QString("font-size: 10px; color: %1;").arg(secondaryText));
                }
            }
        }
    }

    // Update tip frame
    if (m_tipFrame) {
        m_tipFrame->setStyleSheet(QString(
            "#tipFrame {"
            "  background-color: %1;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 12px;"
            "}"
            "#tipFrame QLabel {"
            "  border: none;"
            "}"
        ).arg(tipBg));
    }

    if (m_tipTitleLabel) {
        m_tipTitleLabel->setStyleSheet(QString("font-size: 10px; font-weight: bold; color: %1; letter-spacing: 1px; text-transform: uppercase; border: none;").arg(secondaryText));
    }
    if (m_tipTextLabel) {
        m_tipTextLabel->setStyleSheet(QString("font-size: 13px; color: %1; border: none;").arg(secondaryText));
    }

    update();
}

bool WelcomeScreenUI::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget *widget = qobject_cast<QWidget*>(watched);
        if (widget) {
            QString filePath = widget->property("filePath").toString();
            if (!filePath.isEmpty()) {
                emit recentProjectRequested(filePath);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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

void WelcomeScreenUI::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml)
{
    // 欢迎界面不管理项目上下文，空实现
    Q_UNUSED(model);
    Q_UNUSED(path);
    Q_UNUSED(name);
    Q_UNUSED(projectXml);
}

QStandardItemModel* WelcomeScreenUI::projectModel() const
{
    return nullptr;
}

QString WelcomeScreenUI::projectPath() const
{
    return QString();
}

QString WelcomeScreenUI::projectName() const
{
    return QString();
}

XMLFile* WelcomeScreenUI::projectXml() const
{
    return nullptr;
}

void WelcomeScreenUI::initTheme()
{
    // 欢迎界面在 activate() 时会检测主题并应用
    updateThemeStyles();
}

void WelcomeScreenUI::setTheme(const QString& theme)
{
    m_currentTheme = theme;

    // Set theme-background property for components to detect theme
    QColor bgColor;
    if (theme == "dark") {
        bgColor = QColor(26, 28, 28);  // #1A1C1C
        this->setProperty("theme-background", QColor(26, 28, 28));
    } else if (theme == "light") {
        bgColor = QColor(249, 249, 249);  // #F9F9F9
        this->setProperty("theme-background", QColor(249, 249, 249));
    } else {  // fusion
        bgColor = QColor(240, 240, 240);  // #F0F0F0
        this->setProperty("theme-background", QColor(240, 240, 240));
    }

    // Apply background color
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, bgColor);
    this->setPalette(palette);

    updateThemeStyles();
}

void WelcomeScreenUI::refreshProjectTree()
{
    // 欢迎界面无需刷新项目树
}

void WelcomeScreenUI::clear()
{
    // 欢迎界面无需清空操作
}

void WelcomeScreenUI::paintEvent(QPaintEvent *event)
{
    // Don't fill background - let parent window's stylesheet control it
    // Only draw the watermark
    QPainter painter(this);

    QPixmap bgPixmap(BIGICON_BG);
    if (!bgPixmap.isNull()) {
        // Scale to large watermark size
        int size = qMin(width(), height()) * 0.6;
        QPixmap scaled = bgPixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        if (m_isDarkTheme) {
            // 反转颜色：将深色 Logo 变为亮色以适应暗色背景，保留透明通道
            QImage img = scaled.toImage();
            img.invertPixels(QImage::InvertRgb);
            scaled = QPixmap::fromImage(img);
            painter.setOpacity(0.05); // 深色主题下的亮色水印透明度
        } else {
            painter.setOpacity(0.04); // 浅色主题下的深色水印透明度
        }

        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    }

    painter.setOpacity(1.0);
}
