#include "QtNodes/internal/NodeDetailWindow.hpp"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QRadioButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QFrame>
#include <QFont>
#include <QVariant>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QStyle>
#include <QSize>
#include <QSizePolicy>
#include <QDebug>
#include <QFileInfo>
#include "ImageView.h"
#include "QtNodes/internal/StyleCollection.hpp"
#include <QStyledItemDelegate>
#include <QHelpEvent>
#include <QToolTip>

namespace QtNodes {

NodeDetailWindow::NodeDetailWindow(QWidget* parent)
    : QDialog(parent)
    , _closeButton(nullptr)
    , _titleCloseButton(nullptr)
    , _titleIcon(nullptr)
    , _titleText(nullptr)
    , _titleState(nullptr)
    , _titleBarWidget(nullptr)
    , _sidebarWidget(nullptr)
    , _stackedWidget(nullptr)
    , _dataViewWidget(nullptr)
    , _validationViewWidget(nullptr)
    , _currentValidationWidget(nullptr)
    , _navGroup(nullptr)
    , _dataViewBtn(nullptr)
    , _validationBtn(nullptr)
    , _inputWidget(nullptr)
    , _processingWidget(nullptr)
    , _outputWidget(nullptr)
    , _footerWidget(nullptr)
    , _inputLayout(nullptr)
    , _processingLayout(nullptr)
    , _outputLayout(nullptr)
{
    setModal(false);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint);

    // Set stylesheet
    setStyleSheet(getThemeStylesheet(parent));

    // Set initial opacity for animation
    setWindowOpacity(0.01);

    setupUI();
}

void NodeDetailWindow::setupUI()
{
    // Main layout
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Title bar
    _titleBarWidget = createTitleBar();
    mainLayout->addWidget(_titleBarWidget);

    // Center area (Sidebar + StackedWidget)
    auto* centerLayout = new QHBoxLayout();
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // 1. Sidebar on the left
    _sidebarWidget = createSidebar();
    centerLayout->addWidget(_sidebarWidget);

    // 2. Stacked widget on the right
    _stackedWidget = new QStackedWidget(this);
    
    // Page 0: Data View Widget
    _dataViewWidget = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(_dataViewWidget);
    contentLayout->setSpacing(10);
    contentLayout->setContentsMargins(12, 12, 12, 12);

    _inputWidget = createInputSection();
    contentLayout->addWidget(_inputWidget);

    _leftSeparator = createColumnSeparator();
    contentLayout->addWidget(_leftSeparator);

    _processingWidget = createProcessingSection();
    contentLayout->addWidget(_processingWidget, 1);  // 中间列stretch填充

    _rightSeparator = createColumnSeparator();
    contentLayout->addWidget(_rightSeparator);

    _outputWidget = createOutputSection();
    contentLayout->addWidget(_outputWidget);

    _stackedWidget->addWidget(_dataViewWidget);

    // Page 1: Validation View Widget
    _validationViewWidget = new QWidget(this);
    auto* validationLayout = new QVBoxLayout(_validationViewWidget);
    validationLayout->setContentsMargins(12, 12, 12, 12);
    validationLayout->setSpacing(10);
    _stackedWidget->addWidget(_validationViewWidget);

    // Page 2: 干涉测量分析视图（第3个选项卡）
    _interferometryViewWidget = new QWidget(this);
    auto* interferometryLayout = new QVBoxLayout(_interferometryViewWidget);
    interferometryLayout->setContentsMargins(12, 12, 12, 12);
    interferometryLayout->setSpacing(10);
    _stackedWidget->addWidget(_interferometryViewWidget);

    centerLayout->addWidget(_stackedWidget, 1);
    mainLayout->addLayout(centerLayout, 1);

    // Footer
    _footerWidget = createFooter();
    mainLayout->addWidget(_footerWidget);

    setMinimumWidth(DETAIL_WINDOW_MIN_WIDTH);
    setMinimumHeight(550);
}

QWidget* NodeDetailWindow::createTitleBar()
{
    bool isDark = isDarkTheme(this);

    auto* titleBar = new QFrame();
    titleBar->setObjectName("TitleBar");

    auto* layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(8);

    // Spacer on left
    layout->addStretch();

    // Blue icon square
    _titleIcon = new QLabel();
    _titleIcon->setFixedSize(12, 12);
    _titleIcon->setStyleSheet("background-color: #2563EB; border-radius: 2px;");
    layout->addWidget(_titleIcon);

    // Title text container
    auto* titleContainer = new QWidget();
    auto* titleLayout = new QHBoxLayout(titleContainer);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);

    _titleText = new QLabel();
    _titleText->setObjectName("TitleText");
    titleLayout->addWidget(_titleText);

    _titleState = new QLabel();
    _titleState->setObjectName("StateText");
    titleLayout->addWidget(_titleState);

    layout->addWidget(titleContainer);

    // Spacer to center the title
    layout->addStretch();

    // X close button on the right - size controlled by StyleSheet min/max-width/height
    _titleCloseButton = new QPushButton();
    _titleCloseButton->setFlat(true);
    _titleCloseButton->setCursor(Qt::PointingHandCursor);
    _titleCloseButton->setIconSize(QSize(12, 12));

    // Load close icon from resources
    QIcon closeIcon(":/ads/images/close-button.svg");
    _titleCloseButton->setIcon(closeIcon);

    QString closeBtnColor = isDark ? "#9CA3AF" : "#6B7280";
    QString closeBtnHover = isDark ? "#F3F4F6" : "#374151";
    _titleCloseButton->setStyleSheet(QString(
        "QPushButton {"
        "  color: %1;"
        "  padding: 0px;"
        "  border: none;"
        "  background-color: rgba(100,100,100,0.15);"
        "  border-radius: 2px;"
        "  min-width: 14px;"
        "  min-height: 14px;"
        "  max-width: 14px;"
        "  max-height: 14px;"
        "}"
        "QPushButton:hover { background-color: rgba(100,100,100,0.3); color: %2; }"
        "QPushButton:disabled { color: %1; }"
    ).arg(closeBtnColor).arg(closeBtnHover));
    connect(_titleCloseButton, &QPushButton::clicked, this, &NodeDetailWindow::closeRequested);
    layout->addWidget(_titleCloseButton);

    return titleBar;
}

QWidget* NodeDetailWindow::createSidebar()
{
    bool isDark = isDarkTheme(this);
    
    QFrame* sidebar = new QFrame();
    sidebar->setObjectName("Sidebar");
    sidebar->setFixedWidth(54); // sidebar width
    
    QString sidebarStyle = isDark ? 
        "QFrame#Sidebar { background-color: #111827; border-right: 1px solid #1F2937; }" :
        "QFrame#Sidebar { background-color: #F3F4F6; border-right: 1px solid #D1D5DB; }";
    sidebar->setStyleSheet(sidebarStyle);

    QVBoxLayout* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 16, 0, 0);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    // 1. Data view button
    _dataViewBtn = new QToolButton();
    _dataViewBtn->setCheckable(true);
    _dataViewBtn->setChecked(true);
    _dataViewBtn->setText(QObject::tr("数据"));
    _dataViewBtn->setToolTip(QObject::tr("查看输入输出数据"));
    _dataViewBtn->setFixedSize(42, 42);
    _dataViewBtn->setCursor(Qt::PointingHandCursor);

    // 2. Validation view button
    _validationBtn = new QToolButton();
    _validationBtn->setCheckable(true);
    _validationBtn->setText(QObject::tr("验证"));
    _validationBtn->setToolTip(QObject::tr("数据正确性验证"));
    _validationBtn->setFixedSize(42, 42);
    _validationBtn->setCursor(Qt::PointingHandCursor);

    // Button style sheets (sleek modern design)
    QString btnStyle = isDark ?
        "QToolButton {"
        "  color: #9CA3AF;"
        "  background-color: transparent;"
        "  border: none;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
        "QToolButton:hover {"
        "  color: #F3F4F6;"
        "  background-color: #1F2937;"
        "}"
        "QToolButton:checked {"
        "  color: #60A5FA;"
        "  background-color: #1F2937;"
        "  font-weight: 600;"
        "}"
        "QToolButton:disabled {"
        "  color: #4B5563;"
        "  background-color: transparent;"
        "}" :
        "QToolButton {"
        "  color: #4B5563;"
        "  background-color: transparent;"
        "  border: none;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
        "QToolButton:hover {"
        "  color: #111827;"
        "  background-color: #E5E7EB;"
        "}"
        "QToolButton:checked {"
        "  color: #2563EB;"
        "  background-color: #E5E7EB;"
        "  font-weight: 600;"
        "}"
        "QToolButton:disabled {"
        "  color: #D1D5DB;"
        "  background-color: transparent;"
        "}";
    
    _dataViewBtn->setStyleSheet(btnStyle);
    _validationBtn->setStyleSheet(btnStyle);

    // 3. 干涉测量分析按钮（第3个选项卡）
    _interferometryBtn = new QToolButton();
    _interferometryBtn->setCheckable(true);
    _interferometryBtn->setText(QObject::tr("干涉"));
    _interferometryBtn->setToolTip(QObject::tr("干涉测量分析：评估影像对是否适合进行InSAR处理"));
    _interferometryBtn->setFixedSize(42, 42);
    _interferometryBtn->setCursor(Qt::PointingHandCursor);
    _interferometryBtn->setStyleSheet(btnStyle);
    _interferometryBtn->setEnabled(false);        // 初始禁用，loadData 时由节点决定是否启用
    _interferometryBtn->setVisible(false);         // 初始隐藏

    // Group the buttons to ensure exclusive selection
    _navGroup = new QButtonGroup(this);
    _navGroup->addButton(_dataViewBtn, 0);
    _navGroup->addButton(_validationBtn, 1);
    _navGroup->addButton(_interferometryBtn, 2);   // 注册为第3个按钮（id=2）
    _navGroup->setExclusive(true);

    layout->addWidget(_dataViewBtn);
    layout->addWidget(_validationBtn);
    layout->addWidget(_interferometryBtn);

    // Connect page switching
    connect(_navGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int id) {
        if (_stackedWidget) {
            _stackedWidget->setCurrentIndex(id);
        }
    });

    return sidebar;
}

QWidget* NodeDetailWindow::createFooter()
{
    bool isDark = isDarkTheme(this);

    auto* footer = new QFrame();
    footer->setObjectName("Footer");

    auto* layout = new QHBoxLayout(footer);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignCenter);

    _closeButton = new QPushButton(QObject::tr("Close"));
    QString footerStyle = isDark ? STYLE_CLOSE_BUTTON_DARK : STYLE_CLOSE_BUTTON_LIGHT;
    _closeButton->setStyleSheet(footerStyle);
    connect(_closeButton, &QPushButton::clicked, this, &NodeDetailWindow::closeRequested);
    layout->addWidget(_closeButton);

    return footer;
}

QWidget* NodeDetailWindow::createColumnSeparator()
{
    bool isDark = isDarkTheme(this);
    auto* line = new QFrame();
    line->setObjectName("ColumnSeparator");
    line->setFixedWidth(1);
    line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    line->setStyleSheet(QString(
        "background-color: %1;"
    ).arg(isDark ? "#374151" : "#D1D5DB"));
    return line;
}

QWidget* NodeDetailWindow::createInputSection()
{
    bool isDark = isDarkTheme(this);

    auto* container = new QFrame();
    container->setObjectName("DetailCard");
    container->setMinimumWidth(SIDE_SECTION_MIN_WIDTH);
    container->setMaximumWidth(SIDE_SECTION_MAX_WIDTH);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel(QObject::tr("Input Data"));
    title->setStyleSheet(isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    scrollArea->setStyleSheet(STYLE_SCROLL_AREA);

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(STYLE_SCROLL_CONTENT);
    _inputLayout = new QVBoxLayout(scrollContent);
    _inputLayout->setContentsMargins(2, 2, 2, 2);
    _inputLayout->setSpacing(4);

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea, 1);

    return container;
}

QWidget* NodeDetailWindow::createProcessingSection()
{
    bool isDark = isDarkTheme(this);

    auto* container = new QFrame();
    container->setObjectName("MiddleColumn");
    container->setMinimumWidth(PROCESSING_SECTION_MIN_WIDTH);
    container->setStyleSheet(isDark ? STYLE_MIDDLE_COLUMN_DARK : STYLE_MIDDLE_COLUMN_LIGHT);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel(QObject::tr("Processing Info"));
    title->setStyleSheet(isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    scrollArea->setStyleSheet(STYLE_SCROLL_AREA);

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(STYLE_SCROLL_CONTENT);
    _processingLayout = new QVBoxLayout(scrollContent);
    _processingLayout->setContentsMargins(2, 2, 2, 2);
    _processingLayout->setSpacing(4);

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea, 1);

    return container;
}

QWidget* NodeDetailWindow::createOutputSection()
{
    bool isDark = isDarkTheme(this);

    auto* container = new QFrame();
    container->setObjectName("DetailCard");
    container->setMinimumWidth(SIDE_SECTION_MIN_WIDTH);
    container->setMaximumWidth(SIDE_SECTION_MAX_WIDTH);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel(QObject::tr("Output Data"));
    title->setStyleSheet(isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    scrollArea->setStyleSheet(STYLE_SCROLL_AREA);

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(STYLE_SCROLL_CONTENT);
    _outputLayout = new QVBoxLayout(scrollContent);
    _outputLayout->setContentsMargins(2, 2, 2, 2);
    _outputLayout->setSpacing(4);
    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea, 1);

    return container;
}

void NodeDetailWindow::renderPortCard(QVBoxLayout* layout, const PortDataInfo& info, bool isOutput, QWidget* parent)
{
    bool isDark = isDarkTheme(parent);

    // Create card container with connector position
    QWidget* cardContainer = new QWidget();
    QHBoxLayout* containerLayout = new QHBoxLayout(cardContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // Input connector dot (left side) - align to center to match HTML design
    if (!isOutput) {
        QWidget* connector = new QWidget();
        connector->setFixedSize(8, 8);
        QString connectorColor;
        QString connectorBorder;
        if (info.isConnected) {
            connectorColor = "#3B82F6";
            connectorBorder = "#2563EB";
        } else {
            connectorColor = isDark ? "#6B7280" : "#94A3B8";
            connectorBorder = isDark ? "#4B5563" : "#64748B";
        }
        connector->setStyleSheet(QString(
            "background-color: %1; border: 1px solid %2; border-radius: 4px;"
        ).arg(connectorColor).arg(connectorBorder));

        auto* connectorWrapper = new QWidget();
        connectorWrapper->setFixedWidth(14);
        auto* connectorLayout = new QVBoxLayout(connectorWrapper);
        connectorLayout->setContentsMargins(0, 0, 0, 0);
        connectorLayout->setSpacing(0);
        connectorLayout->addStretch();
        connectorLayout->addWidget(connector, 0, Qt::AlignCenter);
        connectorLayout->addStretch();

        containerLayout->addWidget(connectorWrapper);
    }

    // Main card
    QFrame* card = new QFrame();
    card->setObjectName(isOutput ? "OutputCard" : "PortCard");
    card->setStyleSheet(isDark ?
        (isOutput ? STYLE_OUTPUT_CARD_DARK : STYLE_PORT_CARD_DARK) :
        (isOutput ? STYLE_OUTPUT_CARD_LIGHT : STYLE_PORT_CARD_LIGHT));

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(4);

    // Text colors - ui2.md Section 24: #1A1C1C, Section 145: #1D4ED8
    QString primaryTextColor = isDark ? "#F9FAFB" : "#1A1C1C";
    QString secondaryTextColor = isDark ? "#9CA3AF" : "#6B7280";
    QString tertiaryTextColor = isDark ? "#D1D5DB" : "#374151";
    QString indexColor = secondaryTextColor;
    QString portNameColor = isDark ? "#60A5FA" : "#1D4ED8";

    // Header row: Port name + Status badge
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);

    // Port name with optional index
    QString headerText;
    if (info.showIndex) {
        headerText = QString("<span style='font-weight: 600; font-size: 12px; color: %1; text-transform: none;'>%2</span> "
                             "<span style='color: %3; font-size: 11px;'>[%4]</span>")
            .arg(portNameColor).arg(info.name).arg(indexColor).arg(info.index);
    } else {
        headerText = QString("<span style='font-weight: 600; font-size: 12px; color: %1; text-transform: none;'>%2</span>")
            .arg(portNameColor).arg(info.name);
    }
    QLabel* nameLabel = new QLabel(headerText);
    headerLayout->addWidget(nameLabel);

    headerLayout->addStretch();

    // Status badge
    bool hasData = !info.summary.isEmpty() || info.isConnected || info.isBound;
    QLabel* badge = new QLabel(hasData ? QObject::tr("Ready") : QObject::tr("Empty"));
    badge->setObjectName("Badge");
    badge->setStyleSheet(hasData ? STYLE_BADGE_READY : STYLE_BADGE_EMPTY);
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedHeight(18);  // prevent vertical stretching
    headerLayout->addWidget(badge);

    cardLayout->addLayout(headerLayout);

    // Data type - Type: left, value right
    if (!info.dataType.isEmpty()) {
        auto* typeLayout = new QHBoxLayout();
        typeLayout->setSpacing(4);
        QLabel* typePrefix = new QLabel("Type:");
        typePrefix->setStyleSheet(QString("color: %1; font-size: 11px; text-transform: none;").arg(secondaryTextColor));
        QLabel* typeValue = new QLabel(info.dataType);
        typeValue->setStyleSheet(QString("color: %1; font-size: 11px; text-transform: none;").arg(tertiaryTextColor));
        typeLayout->addWidget(typePrefix);
        typeLayout->addStretch();
        typeLayout->addWidget(typeValue);
        cardLayout->addLayout(typeLayout);
    }

    // Extract Path field and remove redundant File fields
    QString pathValue;
    QVector<DataField> remainingFields;
    bool useSmartFileLayout = false;
    
    if (!info.fields.isEmpty()) {
        for (const auto& field : info.fields) {
            if (field.key == "Path" && !field.value.isEmpty()) {
                pathValue = field.value;
                useSmartFileLayout = true;
            } else if ((field.key == "File" || field.key == "FileName") && field.value == info.summary) {
                // Ignore redundant File field
                useSmartFileLayout = true;
            } else {
                remainingFields.append(field);
            }
        }
    }

    // Summary (preview area)
    if (!info.summary.isEmpty()) {
        QString displaySummary = info.summary;
        if (displaySummary.length() > 45) {
            displaySummary = displaySummary.left(20) + "..." + displaySummary.right(20);
        }

        QWidget* summaryContainer = new QWidget();
        QVBoxLayout* summaryLayout = new QVBoxLayout(summaryContainer);
        summaryLayout->setContentsMargins(0, 0, 0, 0);
        summaryLayout->setSpacing(2);

        if (useSmartFileLayout) {
            // Smart Layout for File/Path
            QLabel* nameLabel = new QLabel(displaySummary);
            nameLabel->setStyleSheet(QString(
                "color: %1;"
                "font-size: 11px;"
                "font-weight: 600;"
                "text-transform: none;"
            ).arg(primaryTextColor));
            nameLabel->setWordWrap(true);
            summaryLayout->addWidget(nameLabel);

            if (!pathValue.isEmpty()) {
                QLabel* pathLabel = new QLabel();
                pathLabel->setStyleSheet(QString(
                    "color: %1;"
                    "font-size: 11px;"
                    "text-transform: none;"
                ).arg(secondaryTextColor));
                
                pathLabel->setToolTip(pathValue);
                pathLabel->setWordWrap(true);
                QStringList pathLines = pathValue.split("\n", Qt::SkipEmptyParts);
                QString displayPath;
                for (int i = 0; i < pathLines.size(); ++i) {
                    QString displayLine = pathLines[i];
                    if (displayLine.length() > 45) {
                        displayLine = displayLine.left(15) + "..." + displayLine.right(25);
                    }
                    if (i > 0) displayPath += "\n";
                    displayPath += displayLine;
                }
                pathLabel->setText(displayPath);
                summaryLayout->addWidget(pathLabel);
            }
        } else {
            // Standard Layout without the heavy border
            QLabel* summaryLabel = new QLabel(QObject::tr("Summary"));
            summaryLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 500; text-transform: none;").arg(secondaryTextColor));
            summaryLayout->addWidget(summaryLabel);

            QLabel* valueLabel = new QLabel(displaySummary);
            valueLabel->setStyleSheet(QString(
                "color: %1;"
                "font-size: 12px;"
                "text-transform: none;"
            ).arg(tertiaryTextColor));
            valueLabel->setWordWrap(true);
            summaryLayout->addWidget(valueLabel);
        }

        cardLayout->addWidget(summaryContainer);
    }

    // Fields section
    if (!remainingFields.isEmpty()) {
        // Separator
        QFrame* separator = new QFrame();
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet(QString("background-color: %1;").arg(isDark ? "#374151" : "#E5E7EB"));
        separator->setFixedHeight(1);
        cardLayout->addWidget(separator);

        for (const auto& field : remainingFields) {
            QHBoxLayout* fieldLayout = new QHBoxLayout();
            fieldLayout->setSpacing(6);

            QLabel* keyLabel = new QLabel(field.key + ":");
            keyLabel->setStyleSheet(QString("color: %1; font-size: 11px; text-transform: none;").arg(secondaryTextColor));
            keyLabel->setMinimumWidth(60);
            fieldLayout->addWidget(keyLabel);

            QString displayValue = field.value;
            if (displayValue.length() > 50) {
                displayValue = displayValue.left(50) + "...";
            }
            QLabel* valueLabel = new QLabel(displayValue);
            valueLabel->setStyleSheet(QString("color: %1; font-size: 11px; text-transform: none;").arg(tertiaryTextColor));
            valueLabel->setWordWrap(true);
            fieldLayout->addWidget(valueLabel, 1);

            cardLayout->addLayout(fieldLayout);
        }
    }

    // Connection status
    if (!info.isConnected) {
        QLabel* statusLabel = nullptr;
        if (info.isBound) {
            statusLabel = new QLabel(info.bindingSummary);
            statusLabel->setStyleSheet(QString("color: %1; font-style: italic; font-size: 11px; text-transform: none;")
                .arg(isDark ? "#60A5FA" : "#2563EB"));
        } else {
            statusLabel = new QLabel(QObject::tr("Not connected"));
            statusLabel->setStyleSheet(QString("color: %1; font-style: italic; font-size: 11px; text-transform: none;")
                .arg(isDark ? "#F87171" : "#DC2626"));
        }
        cardLayout->addWidget(statusLabel);
    }

    containerLayout->addWidget(card);

    // Output connector dot (right side) - align to center to match HTML design
    if (isOutput) {
        QWidget* connector = new QWidget();
        connector->setFixedSize(8, 8);
        QString connectorColor;
        QString connectorBorder;
        if (info.isConnected) {
            connectorColor = "#3B82F6";
            connectorBorder = "#2563EB";
        } else {
            connectorColor = isDark ? "#6B7280" : "#94A3B8";
            connectorBorder = isDark ? "#4B5563" : "#64748B";
        }
        connector->setStyleSheet(QString(
            "background-color: %1; border: 1px solid %2; border-radius: 4px;"
        ).arg(connectorColor).arg(connectorBorder));

        auto* connectorWrapper = new QWidget();
        connectorWrapper->setFixedWidth(14);
        auto* connectorLayout = new QVBoxLayout(connectorWrapper);
        connectorLayout->setContentsMargins(0, 0, 0, 0);
        connectorLayout->setSpacing(0);
        connectorLayout->addStretch();
        connectorLayout->addWidget(connector, 0, Qt::AlignCenter);
        connectorLayout->addStretch();

        containerLayout->addWidget(connectorWrapper);
    }

    layout->addWidget(cardContainer);
}

void NodeDetailWindow::renderParameterCard(QVBoxLayout* layout, const ParameterInfo& param, QWidget* parent)
{
    bool isDark = isDarkTheme(parent);

    QFrame* card = new QFrame();
    card->setObjectName("PortCard");
    card->setStyleSheet(isDark ? STYLE_PORT_CARD_DARK : STYLE_PORT_CARD_LIGHT);

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(4);

    // Text colors
    QString primaryTextColor = isDark ? "#F9FAFB" : "#1F2937";
    QString secondaryTextColor = isDark ? "#9CA3AF" : "#6B7280";
    QString tertiaryTextColor = isDark ? "#D1D5DB" : "#374155";

    // Parameter name with bold styling
    QLabel* nameLabel = new QLabel(QString("<span style='font-weight: 600; font-size: 12px; color: %1; text-transform: none;'>%2</span>")
        .arg(primaryTextColor).arg(param.name));
    cardLayout->addWidget(nameLabel);

    // Data type - Type: left, value right
    auto* typeLayout = new QHBoxLayout();
    typeLayout->setSpacing(4);
    QLabel* typePrefix = new QLabel("Type:");
    typePrefix->setStyleSheet(QString("color: %1; font-size: 11px; text-transform: none;").arg(secondaryTextColor));
    QLabel* typeValue = new QLabel(param.dataType);
    typeValue->setStyleSheet(QString("color: %1; font-size: 11px; text-transform: none;").arg(tertiaryTextColor));
    typeLayout->addWidget(typePrefix);
    typeLayout->addStretch();
    typeLayout->addWidget(typeValue);
    cardLayout->addLayout(typeLayout);

    // Display value
    QString displayValue = param.value;
    if (displayValue.length() > 60) {
        displayValue = displayValue.left(60) + "...";
    }

    QLabel* valueLabel = new QLabel(displayValue);
    QString valueBg = isDark ? "#1F2937" : "#F9FAFB";
    QString valueBorder = isDark ? "#374151" : "#E5E7EB";
    valueLabel->setStyleSheet(QString(
        "QLabel {"
        "   color: %1;"
        "   font-size: 12px;"
        "   background-color: %2;"
        "   border: 1px solid %3;"
        "   padding: 5px 6px;"
        "   border-radius: 4px;"
        "   border-left: 3px solid #3B82F6;"
        "}"
    ).arg(tertiaryTextColor).arg(valueBg).arg(valueBorder));
    valueLabel->setWordWrap(true);
    cardLayout->addWidget(valueLabel);

    layout->addWidget(card);
}

void NodeDetailWindow::loadData(const NodeDataSnapshot& snapshot, ExecutableNodeDelegateModel* model)
{
    bool isDark = isDarkTheme(this);

    // Set title text and state
    _titleText->setText(snapshot.nodeName);
    _titleState->setText(QString("(%1)").arg(NodeDataSnapshot::stateToString(snapshot.state)));

    int oldIndex = _currentPreviewIndex;

    // Clear previous data
    clearData();

    // Store data
    _previewImagePaths = snapshot.previewImagePaths;
    _detectionResults = snapshot.detectionResults;
    _inputPorts = snapshot.inputPorts;
    _parameters = snapshot.parameters;
    _processingInfo = snapshot.processingInfo;
    _outputPorts = snapshot.outputPorts;
    _tableHeaders = snapshot.detailTableHeaders;
    _supportsRoiSelection = snapshot.supportsRoiSelection;
    _hasCustomRoi = snapshot.hasCustomRoi;
    _customRoi = snapshot.customRoi;
    _supportsTwoRois = snapshot.supportsTwoRois;
    _hasTargetRoi = snapshot.hasTargetRoi;
    _targetRoi = snapshot.targetRoi;
    _hasClutterRoi = snapshot.hasClutterRoi;
    _clutterRoi = snapshot.clutterRoi;

    // Restore index if within bounds
    if (oldIndex >= 0 && oldIndex < _previewImagePaths.size()) {
        _currentPreviewIndex = oldIndex;
    } else {
        _currentPreviewIndex = 0;
    }
    
    updateLayoutVisibility();

    // ===== INPUT SECTION: Port Data + Node Parameters =====
    bool hasInputContent = false;
    QString sectionTextColor = isDark ? "#9CA3AF" : "#6B7280";

    // Input ports first
    if (!_inputPorts.empty()) {

        for (size_t i = 0; i < _inputPorts.size(); ++i) {
            renderPortCard(_inputLayout, _inputPorts[i], false, this);
        }
        hasInputContent = true;
    }

    // Add node parameters if any
    if (!_parameters.empty()) {
        if (hasInputContent) {
            // Add separator between ports and parameters
            QFrame* separator = new QFrame();
            separator->setFrameShape(QFrame::HLine);
            separator->setStyleSheet(QString("background-color: %1;").arg(isDark ? "#374151" : "#E5E7EB"));
            separator->setFixedHeight(1);
            QWidget* spacer = new QWidget();
            spacer->setFixedHeight(8);
            _inputLayout->addWidget( spacer);
            _inputLayout->addWidget( separator);
            _inputLayout->addWidget( spacer);
        }

        QLabel* paramsLabel = new QLabel(QObject::tr("Node Parameters"));
        paramsLabel->setStyleSheet(QString(
            "color: %1; font-weight: bold; font-size: 11px; margin-bottom: 4px;"
        ).arg(sectionTextColor));
        _inputLayout->addWidget( paramsLabel);

        for (const auto& param : _parameters) {
            renderParameterCard(_inputLayout, param, this);
        }
        hasInputContent = true;
    }

    // Show "No content" message if nothing to display
    if (!hasInputContent) {
        QLabel* noContentLabel = new QLabel(QObject::tr("No input ports or parameters"));
        noContentLabel->setStyleSheet(QString(
            "color: %1; font-style: italic; font-size: 11px;"
        ).arg(sectionTextColor));
        noContentLabel->setAlignment(Qt::AlignCenter);
        _inputLayout->addWidget( noContentLabel);
    }
    _inputLayout->addStretch(1);

    // ===== PROCESSING INFO SECTION =====
    QString infoLabelTemplate = isDark ? STYLE_INFO_LABEL_TEMPLATE_DARK : STYLE_INFO_LABEL_TEMPLATE;
    
    _previewImagePaths = snapshot.previewImagePaths;
    _detectionResults = snapshot.detectionResults;
    
    // Restore index if within bounds
    if (oldIndex >= 0 && oldIndex < _previewImagePaths.size()) {
        _currentPreviewIndex = oldIndex;
    } else {
        _currentPreviewIndex = 0;
    }
    
    bool hasPreviewImage = !_previewImagePaths.isEmpty() && QFileInfo::exists(_previewImagePaths.first());
    
    if (hasPreviewImage) {
        _imageView = new ImageView();
        _imageView->setStyleSheet(QString("border: 1px solid %1; border-radius: 4px;")
            .arg(isDarkTheme(this) ? "#4B5563" : "#E5E7EB"));
        _imageView->setMinimumHeight(240);
        _imageView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

        bool isDark = isDarkTheme(this);
        QString toggleBtnStyle = QString(
            "QPushButton {"
            "  padding: 2px 8px;"
            "  font-size: 12px;"
            "  border: 1px solid %1;"
            "  border-radius: 4px;"
            "  background-color: %2;"
            "  color: %3;"
            "  min-width: 0px;"
            "}"
            "QPushButton:hover {"
            "  background-color: %4;"
            "}"
            "QPushButton:checked {"
            "  background-color: #3B82F6;"
            "  border-color: #3B82F6;"
            "  color: white;"
            "  font-weight: bold;"
            "}")
            .arg(isDark ? "#4B5563" : "#D1D5DB")
            .arg(isDark ? "#374151" : "#F9FAFB")
            .arg(isDark ? "#F9FAFB" : "#374151")
            .arg(isDark ? "#4B5563" : "#F3F4F6");
            
        QString clearBtnStyle = QString(
            "QPushButton {"
            "  padding: 2px 6px;"
            "  font-size: 12px;"
            "  border: 1px solid %1;"
            "  border-radius: 4px;"
            "  background-color: %2;"
            "  color: #EF4444;"
            "  min-width: 0px;"
            "}"
            "QPushButton:hover {"
            "  background-color: %3;"
            "}")
            .arg(isDark ? "#4B5563" : "#D1D5DB")
            .arg(isDark ? "#1F2937" : "#FFFFFF")
            .arg(isDark ? "#7F1D1D" : "#FEE2E2");

        _roiToolbar = new QWidget();
        auto* roiLayout = new QHBoxLayout(_roiToolbar);
        roiLayout->setContentsMargins(0, 2, 0, 2);
        roiLayout->setSpacing(4);

        _moveModeBtn = new QPushButton(QStringLiteral("移动"));
        _moveModeBtn->setCheckable(true);
        _moveModeBtn->setChecked(!_hasCustomRoi && !_hasTargetRoi && !_hasClutterRoi);
        _moveModeBtn->setStyleSheet(toggleBtnStyle);
        connect(_moveModeBtn, &QPushButton::clicked, this, &NodeDetailWindow::onMoveModeClicked);

        _zoomInBtn = new QPushButton(QStringLiteral("放大"));
        _zoomInBtn->setStyleSheet(toggleBtnStyle);
        connect(_zoomInBtn, &QPushButton::clicked, this, &NodeDetailWindow::onZoomInClicked);

        _zoomOutBtn = new QPushButton(QStringLiteral("缩小"));
        _zoomOutBtn->setStyleSheet(toggleBtnStyle);
        connect(_zoomOutBtn, &QPushButton::clicked, this, &NodeDetailWindow::onZoomOutClicked);

        _fitImageBtn = new QPushButton(QStringLiteral("适应"));
        _fitImageBtn->setStyleSheet(toggleBtnStyle);
        connect(_fitImageBtn, &QPushButton::clicked, this, &NodeDetailWindow::onFitImageClicked);

        roiLayout->addWidget(_moveModeBtn);
        roiLayout->addWidget(_zoomInBtn);
        roiLayout->addWidget(_zoomOutBtn);
        roiLayout->addWidget(_fitImageBtn);

        if (_supportsRoiSelection || _supportsTwoRois) {
            QFrame* vLine = new QFrame();
            vLine->setFrameShape(QFrame::VLine);
            vLine->setFrameShadow(QFrame::Sunken);
            vLine->setStyleSheet(isDark ? "background-color: #4B5563;" : "background-color: #D1D5DB;");
            roiLayout->addWidget(vLine);
        }

        if (_supportsRoiSelection) {
            _roiEnableBtn = new QPushButton(QStringLiteral("框选"));
            _roiEnableBtn->setCheckable(true);
            _roiEnableBtn->setStyleSheet(toggleBtnStyle);
            _roiEnableBtn->setChecked(_hasCustomRoi);
            _imageView->setRoiSelectionEnabled(_hasCustomRoi);
            if (_hasCustomRoi) {
                _imageView->setRoiRect(_customRoi);
            }
            connect(_roiEnableBtn, &QPushButton::toggled, this, &NodeDetailWindow::onRoiToggled);

            auto* clearRoiBtn = new QPushButton(QStringLiteral("清除"));
            clearRoiBtn->setStyleSheet(clearBtnStyle);
            connect(clearRoiBtn, &QPushButton::clicked, this, &NodeDetailWindow::onRoiCleared);

            roiLayout->addWidget(_roiEnableBtn);
            roiLayout->addWidget(clearRoiBtn);

            connect(_imageView, &ImageView::roiSelected, this, [this](const QRectF& rect) {
                emit roiSelectionChanged(rect, _currentPreviewIndex);
            });
        } else if (_supportsTwoRois) {
            _targetRoiBtn = new QPushButton(QStringLiteral("目标区域"));
            _targetRoiBtn->setCheckable(true);
            _targetRoiBtn->setStyleSheet(toggleBtnStyle);

            _clutterRoiBtn = new QPushButton(QStringLiteral("杂波区域"));
            _clutterRoiBtn->setCheckable(true);
            _clutterRoiBtn->setStyleSheet(toggleBtnStyle);

            if (_hasTargetRoi) _imageView->setTargetRoiRect(_targetRoi);
            if (_hasClutterRoi) _imageView->setClutterRoiRect(_clutterRoi);

            connect(_targetRoiBtn, &QPushButton::toggled, this, &NodeDetailWindow::onTargetRoiToggled);
            connect(_clutterRoiBtn, &QPushButton::toggled, this, &NodeDetailWindow::onClutterRoiToggled);

            auto* clearTargetBtn = new QPushButton(QStringLiteral("清除"));
            clearTargetBtn->setStyleSheet(clearBtnStyle);

            auto* clearClutterBtn = new QPushButton(QStringLiteral("清除"));
            clearClutterBtn->setStyleSheet(clearBtnStyle);

            connect(clearTargetBtn, &QPushButton::clicked, this, &NodeDetailWindow::onTargetRoiCleared);
            connect(clearClutterBtn, &QPushButton::clicked, this, &NodeDetailWindow::onClutterRoiCleared);

            roiLayout->addWidget(_targetRoiBtn);
            roiLayout->addWidget(clearTargetBtn);
            roiLayout->addWidget(_clutterRoiBtn);
            roiLayout->addWidget(clearClutterBtn);

            connect(_imageView, &ImageView::targetRoiSelected, this, [this](const QRectF& rect) {
                emit targetRoiSelectionChanged(rect, _currentPreviewIndex);
            });
            connect(_imageView, &ImageView::clutterRoiSelected, this, [this](const QRectF& rect) {
                emit clutterRoiSelectionChanged(rect, _currentPreviewIndex);
            });
        }

        roiLayout->addStretch();
        _processingLayout->addWidget(_roiToolbar);
        _processingLayout->addWidget(_imageView);

        QHBoxLayout* navLayout = new QHBoxLayout();
        
        QString btnStyle = isDark ? 
            "QPushButton { min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; padding: 0px; margin: 0px; border: none; background: #4B5563; border-radius: 12px; font-weight: bold; color: #F3F4F6; }"
            "QPushButton:hover:!disabled { background: #6B7280; }"
            "QPushButton:pressed { background: #374151; }"
            "QPushButton:disabled { background: #374151; color: #9CA3AF; }"
            :
            "QPushButton { min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; padding: 0px; margin: 0px; border: none; background: #E5E7EB; border-radius: 12px; font-weight: bold; color: #374151; }"
            "QPushButton:hover:!disabled { background: #D1D5DB; }"
            "QPushButton:pressed { background: #9CA3AF; }"
            "QPushButton:disabled { background: #F3F4F6; color: #9CA3AF; }";
            
        _prevButton = new QPushButton("<");
        _prevButton->setFixedSize(24, 24);
        _prevButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        _prevButton->setStyleSheet(btnStyle);
        
        _imageNameLabel = new QLabel();
        _imageNameLabel->setAlignment(Qt::AlignCenter);
        
        _nextButton = new QPushButton(">");
        _nextButton->setFixedSize(24, 24);
        _nextButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        _nextButton->setStyleSheet(btnStyle);
        
        navLayout->addWidget(_prevButton);
        navLayout->addWidget(_imageNameLabel, 1);
        navLayout->addWidget(_nextButton);
        
        _processingLayout->addLayout(navLayout);
        
        connect(_prevButton, &QPushButton::clicked, this, &NodeDetailWindow::onPrevPreviewClicked);
        connect(_nextButton, &QPushButton::clicked, this, &NodeDetailWindow::onNextPreviewClicked);
        
        if (_previewImagePaths.size() <= 1) {
            _prevButton->hide();
            _nextButton->hide();
        }
        
        bool hasDetectionResults = !_detectionResults.isEmpty() && (_detectionResults.size() >= _previewImagePaths.size());
        
        if (hasDetectionResults) {
            // Add results table
            _resultsTable = new QTableWidget();
            
            if (!_tableHeaders.isEmpty()) {
                _resultsTable->setColumnCount(_tableHeaders.size());
                _resultsTable->setHorizontalHeaderLabels(_tableHeaders);
                // First column stretch, others resize to contents
                _resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
                for(int c=1; c<_tableHeaders.size(); ++c) {
                    _resultsTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
                }
            } else {
                // Fallback for older nodes
                _resultsTable->setColumnCount(3);
                _resultsTable->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("检测结果"), QStringLiteral("置信度")});
                _resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
                _resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
                _resultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            }
            
            _resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            _resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
            _resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            _resultsTable->verticalHeader()->setVisible(false);
            _resultsTable->setAlternatingRowColors(true);
            _resultsTable->setShowGrid(false);
            
            QString tableStyle = isDark ? 
                "QTableWidget { background-color: #1F2937; alternate-background-color: #374151; border: 1px solid #4B5563; border-radius: 4px; color: #F3F4F6; }"
                "QTableWidget::viewport { background-color: #1F2937; }"
                "QTableWidget::item { padding: 4px 8px; }"
                "QTableWidget::item:selected { background-color: #3B82F6; color: white; }"
                "QHeaderView { background-color: #111827; border: none; }"
                "QHeaderView::section { background-color: #111827; padding: 6px; border: none; border-bottom: 1px solid #4B5563; font-weight: bold; color: #D1D5DB; font-size: 13px; }"
                "QTableCornerButton::section { background-color: #111827; border: none; }" :
                "QTableWidget { background-color: #FFFFFF; alternate-background-color: #F9FAFB; border: 1px solid #E5E7EB; border-radius: 4px; color: #1F2937; }"
                "QTableWidget::viewport { background-color: #FFFFFF; }"
                "QTableWidget::item { padding: 4px 8px; }"
                "QTableWidget::item:selected { background-color: #EFF6FF; color: #1D4ED8; }"
                "QHeaderView { background-color: #F3F4F6; border: none; }"
                "QHeaderView::section { background-color: #F3F4F6; padding: 6px; border: none; border-bottom: 1px solid #E5E7EB; font-weight: bold; color: #4B5563; font-size: 13px; }"
                "QTableCornerButton::section { background-color: #F3F4F6; border: none; }";
            _resultsTable->setStyleSheet(tableStyle);
            
            // Populate table
            _resultsTable->setRowCount(_detectionResults.size());
            for (int i = 0; i < _detectionResults.size(); ++i) {
                const auto& rowData = _detectionResults[i];
                for(int c=0; c<rowData.size() && c<_resultsTable->columnCount(); ++c) {
                    auto* item = new QTableWidgetItem(rowData[c]);
                    if (c > 0) item->setTextAlignment(Qt::AlignCenter);
                    if (c == 1 && rowData[c].toLower() == "ship") {
                        item->setForeground(QBrush(QColor(isDark ? "#34D399" : "#10B981"))); // Green
                    }
                    _resultsTable->setItem(i, c, item);
                }
            }
            
            connect(_resultsTable, &QTableWidget::itemSelectionChanged, this, &NodeDetailWindow::onTableSelectionChanged);
            _processingLayout->addWidget(_resultsTable);
            
            // Adjust proportions
            _imageView->setMinimumHeight(250);
            
            // Dynamic table height based on rows
            int tableHeight = 35 + (_detectionResults.size() * 32) + 15;
            int minTableHeight = 140;
            int maxTableHeight = 400;
            int finalHeight = qBound(minTableHeight, tableHeight, maxTableHeight);
            
            _resultsTable->setMinimumHeight(finalHeight);
            _resultsTable->setMaximumHeight(finalHeight);
            
            if (tableHeight <= maxTableHeight) {
                _resultsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            } else {
                _resultsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            }
        }
        
        // Initialize preview image and sync all newly created UI elements
        updatePreviewImage();
    }
    
    if (_processingInfo.empty() && !hasPreviewImage) {
        // Empty state with info icon
        QFrame* emptyFrame = new QFrame();
        emptyFrame->setObjectName("ProcessingEmpty");
        emptyFrame->setStyleSheet(isDark ? STYLE_PROCESSING_EMPTY_DARK : STYLE_PROCESSING_EMPTY_LIGHT);

        QVBoxLayout* emptyLayout = new QVBoxLayout(emptyFrame);
        emptyLayout->setContentsMargins(8, 8, 8, 8);
        emptyLayout->setSpacing(8);
        emptyLayout->setAlignment(Qt::AlignCenter);

        // Info icon (using Qt standard icon)
        QLabel* iconLabel = new QLabel();
        QIcon infoIcon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        QPixmap pixmap = infoIcon.pixmap(32, 32);
        iconLabel->setPixmap(pixmap);
        iconLabel->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(iconLabel);

        QLabel* noContentLabel = new QLabel(QObject::tr("No processing info available"));
        noContentLabel->setStyleSheet(QString(
            "color: %1; font-style: italic; font-size: 12px; qproperty-alignment: AlignCenter; text-transform: none;"
        ).arg(sectionTextColor));
        emptyLayout->addWidget(noContentLabel);

        _processingLayout->addWidget( emptyFrame);
    } else if (!_processingInfo.empty()) {
        QFrame* infoContainer = new QFrame();
        infoContainer->setObjectName("ProcessingInfoContainer");
        
        QString containerStyle = isDark ? 
            "QFrame#ProcessingInfoContainer { background-color: #374151; border-radius: 4px; }" :
            "QFrame#ProcessingInfoContainer { background-color: #F9FAFB; border-radius: 4px; }";
        infoContainer->setStyleSheet(containerStyle);
        
        QVBoxLayout* containerLayout = new QVBoxLayout(infoContainer);
        containerLayout->setContentsMargins(10, 8, 10, 8);
        containerLayout->setSpacing(6);
        
        for (size_t i = 0; i < _processingInfo.size(); ++i) {
            auto* infoLabel = new QLabel(_processingInfo[i]);
            infoLabel->setWordWrap(true);
            QString labelStyle = isDark ? 
                "QLabel { color: #F3F4F6; font-size: 11px; background: transparent; border: none; padding: 0px; }" :
                "QLabel { color: #374151; font-size: 11px; background: transparent; border: none; padding: 0px; }";
            infoLabel->setStyleSheet(labelStyle);
            containerLayout->addWidget(infoLabel);
        }
        
        _processingLayout->addWidget(infoContainer);
    }
    _processingLayout->addStretch(1);

    // ===== OUTPUT SECTION =====
    if (_outputPorts.empty()) {
        QLabel* noContentLabel = new QLabel(QObject::tr("No output ports"));
        noContentLabel->setStyleSheet(QString(
            "color: %1; font-style: italic; font-size: 11px;"
        ).arg(sectionTextColor));
        noContentLabel->setAlignment(Qt::AlignCenter);
        _outputLayout->addWidget( noContentLabel);
    } else {

        for (const auto& port : _outputPorts) {
            renderPortCard(_outputLayout, port, true, this);
        }
    }
    _outputLayout->addStretch(1);

    // Store model pointer and manage executionStateChanged signal
    if (_model) {
        disconnect(_model, &ExecutableNodeDelegateModel::executionStateChanged, this, nullptr);
    }
    _model = model;
    if (_model) {
        connect(_model, &ExecutableNodeDelegateModel::executionStateChanged, this, [this]() {
            // 运行时禁用验证选项卡
            if (_model && _validationBtn) {
                bool isRunning = (_model->executionState() == ExecutionState::Running);
                _validationBtn->setDisabled(isRunning);
                if (isRunning && _stackedWidget && _stackedWidget->currentIndex() == 1) {
                    _stackedWidget->setCurrentIndex(0);
                    if (_dataViewBtn) {
                        _dataViewBtn->setChecked(true);
                    }
                }
            }
            // 运行时禁用干涉测量选项卡
            if (_model && _interferometryBtn) {
                bool isRunning = (_model->executionState() == ExecutionState::Running);
                _interferometryBtn->setDisabled(isRunning);
                if (isRunning && _stackedWidget && _stackedWidget->currentIndex() == 2) {
                    _stackedWidget->setCurrentIndex(0);
                    if (_dataViewBtn) {
                        _dataViewBtn->setChecked(true);
                    }
                }
            }
        });
    }

    // ===== VALIDATION SECTION =====
    if (model && model->supportsValidation()) {
        if (_validationBtn) {
            bool isRunning = (model->executionState() == ExecutionState::Running);
            _validationBtn->setEnabled(!isRunning);
            _validationBtn->setVisible(true);
        }
        if (_validationViewWidget) {
            // Clean up old validation widget if any
            if (_currentValidationWidget) {
                _validationViewWidget->layout()->removeWidget(_currentValidationWidget);
                _currentValidationWidget->deleteLater();
                _currentValidationWidget = nullptr;
            }
            // Create and add new validation widget
            _currentValidationWidget = model->createValidationWidget(_validationViewWidget);
            if (_currentValidationWidget) {
                _validationViewWidget->layout()->addWidget(_currentValidationWidget);
            }
        }
    } else {
        if (_validationBtn) {
            _validationBtn->setEnabled(false);
            _validationBtn->setVisible(false);
        }
        // Force switch back to Data view page if validation view is selected but not supported
        if (_stackedWidget && _stackedWidget->currentIndex() == 1) {
            _stackedWidget->setCurrentIndex(0);
            if (_dataViewBtn) {
                _dataViewBtn->setChecked(true);
            }
        }
    }

    // ===== 干涉测量分析选项卡 =====
    if (model && model->supportsInterferometry()) {
        if (_interferometryBtn) {
            bool isRunning = (model->executionState() == ExecutionState::Running);
            _interferometryBtn->setEnabled(!isRunning);
            _interferometryBtn->setVisible(true);
        }
        if (_interferometryViewWidget) {
            // 清理旧的干涉测量控件
            if (_currentInterferometryWidget) {
                _interferometryViewWidget->layout()->removeWidget(_currentInterferometryWidget);
                _currentInterferometryWidget->deleteLater();
                _currentInterferometryWidget = nullptr;
            }
            // 创建并添加新的干涉测量控件
            _currentInterferometryWidget = model->createInterferometryWidget(_interferometryViewWidget);
            if (_currentInterferometryWidget) {
                _interferometryViewWidget->layout()->addWidget(_currentInterferometryWidget);
            }
        }
    } else {
        if (_interferometryBtn) {
            _interferometryBtn->setEnabled(false);
            _interferometryBtn->setVisible(false);
        }
        // 如果当前正在查看干涉测量选项卡但不再支持，切回数据视图
        if (_stackedWidget && _stackedWidget->currentIndex() == 2) {
            _stackedWidget->setCurrentIndex(0);
            if (_dataViewBtn) {
                _dataViewBtn->setChecked(true);
            }
        }
    }
}

void NodeDetailWindow::updateTableData(const NodeDataSnapshot& snapshot)
{
    bool hadPreview = (_imageView != nullptr);
    bool hasPreview = !snapshot.previewImagePaths.isEmpty() && QFileInfo::exists(snapshot.previewImagePaths.first());
    
    if (hadPreview != hasPreview || _previewImagePaths.size() != snapshot.previewImagePaths.size()) {
        loadData(snapshot);
        return;
    }
    
    _previewImagePaths = snapshot.previewImagePaths;
    if (_prevButton && _nextButton) {
        if (_previewImagePaths.size() <= 1) {
            _prevButton->hide();
            _nextButton->hide();
        } else {
            _prevButton->show();
            _nextButton->show();
        }
    }
    if (_currentPreviewIndex < 0 || _currentPreviewIndex >= _previewImagePaths.size()) {
        _currentPreviewIndex = 0;
    }
    updatePreviewImage();

    _detectionResults = snapshot.detectionResults;
    if (!_resultsTable) return;
    
    // Clear old contents but keep headers
    _resultsTable->setRowCount(0);
    _resultsTable->setRowCount(_detectionResults.size());
    
    bool isDark = isDarkTheme(this);
    
    for (int i = 0; i < _detectionResults.size(); ++i) {
        const auto& rowData = _detectionResults[i];
        for (int c = 0; c < rowData.size() && c < _resultsTable->columnCount(); ++c) {
            auto* item = new QTableWidgetItem(rowData[c]);
            if (c > 0) item->setTextAlignment(Qt::AlignCenter);
            if (c == 1 && rowData[c].toLower() == "ship") {
                item->setForeground(QBrush(QColor(isDark ? "#34D399" : "#10B981"))); // Green
            }
            _resultsTable->setItem(i, c, item);
        }
    }
}

void NodeDetailWindow::clearData()
{
    if (_currentValidationWidget) {
        if (_validationViewWidget && _validationViewWidget->layout()) {
            _validationViewWidget->layout()->removeWidget(_currentValidationWidget);
        }
        _currentValidationWidget->deleteLater();
        _currentValidationWidget = nullptr;
    }

    // 清理干涉测量选项卡控件（先解除父子托管再删除，杜绝双重释放）
    if (_currentInterferometryWidget) {
        if (_stackedWidget) {
            _stackedWidget->removeWidget(_currentInterferometryWidget);
        }
        _currentInterferometryWidget->deleteLater();
        _currentInterferometryWidget = nullptr;
    }

    std::function<void(QLayout*)> clearLayout = [&](QLayout* layout) {
        if (!layout) return;
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (QLayout* childLayout = item->layout()) {
                clearLayout(childLayout);
                delete childLayout;
            } else {
                if (QWidget* widget = item->widget()) {
                    widget->hide();
                    widget->deleteLater();
                }
                delete item;
            }
        }
    };

    // Clear input section
    clearLayout(_inputLayout);
    _inputPorts.clear();
    _parameters.clear();

    // Clear processing section
    clearLayout(_processingLayout);
    _processingInfo.clear();

    // Clear output section (widgets + stretch spacer)
    clearLayout(_outputLayout);
    _outputPorts.clear();
    
    _previewImagePaths.clear();
    _detectionResults.clear();
    _tableHeaders.clear();
    _supportsRoiSelection = false;
    _hasCustomRoi = false;
    _customRoi = QRectF();
    _roiToolbar = nullptr;
    _roiEnableBtn = nullptr;
    _moveModeBtn = nullptr;
    _zoomInBtn = nullptr;
    _zoomOutBtn = nullptr;
    _fitImageBtn = nullptr;
    _targetRoiBtn = nullptr;
    _clutterRoiBtn = nullptr;
    _imageView = nullptr;
    _imageNameLabel = nullptr;
    _prevButton = nullptr;
    _nextButton = nullptr;
    _imageOverlayLabel = nullptr;
    _resultsTable = nullptr;
    
    updateLayoutVisibility();
}

void NodeDetailWindow::updateLayoutVisibility()
{
    bool hasInput = !_inputPorts.empty() || !_parameters.empty();
    bool hasOutput = !_outputPorts.empty();
    
    if (_inputWidget) _inputWidget->setVisible(hasInput);
    if (_leftSeparator) _leftSeparator->setVisible(hasInput);
    
    if (_outputWidget) _outputWidget->setVisible(hasOutput);
    if (_rightSeparator) _rightSeparator->setVisible(hasOutput);
}

// ============================================================================
// Theme Detection Helper Functions
// ============================================================================

bool NodeDetailWindow::isDarkTheme(QWidget* widget)
{
    while (widget) {
        QVariant bgColor = widget->property("theme-background");
        if (bgColor.isValid()) {
            QColor color = bgColor.value<QColor>();
            // Dark theme: dark background colors
            if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
                return true;
            }
            return false; // Found property but it's light
        }
        widget = widget->parentWidget();
    }

    return false;
}

QString NodeDetailWindow::getThemeStylesheet(QWidget* parent)
{
    bool isDark = isDarkTheme(parent);

    // Window background
    QString windowBg = isDark ? STYLE_WINDOW_DARK : STYLE_WINDOW_LIGHT;

    // Title bar
    QString titleBar = isDark ? STYLE_TITLE_BAR_DARK : STYLE_TITLE_BAR_LIGHT;

    // Title text
    QString titleText = isDark ? STYLE_TITLE_TEXT_DARK : STYLE_TITLE_TEXT_LIGHT;
    QString stateText = isDark ? STYLE_STATE_TEXT_DARK : STYLE_STATE_TEXT_LIGHT;

    // Section titles
    QString sectionTitle = isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT;

    // Card frame
    QString card = isDark ? STYLE_CARD_DARK : STYLE_CARD;

    // Scroll area
    QString scrollArea = STYLE_SCROLL_AREA;

    // Scrollbar
    QString scrollbar = isDark ? STYLE_SCROLLBAR_DARK : STYLE_SCROLLBAR_LIGHT;

    // Footer
    QString footer = isDark ? STYLE_FOOTER_DARK : STYLE_FOOTER_LIGHT;

    // Middle column background
    QString middleColumn = isDark ? STYLE_MIDDLE_COLUMN_DARK : STYLE_MIDDLE_COLUMN_LIGHT;

    // Column separator
    QString columnSeparator = isDark ? STYLE_COLUMN_SEPARATOR_DARK : STYLE_COLUMN_SEPARATOR_LIGHT;

    // Port cards
    QString portCard = isDark ? STYLE_PORT_CARD_DARK : STYLE_PORT_CARD_LIGHT;
    QString outputCard = isDark ? STYLE_OUTPUT_CARD_DARK : STYLE_OUTPUT_CARD_LIGHT;

    // Combine all styles
    return QString("%1%2%3%4%5%6%7%8%9%10%11%12%13")
        .arg(windowBg)
        .arg(titleBar)
        .arg(titleText)
        .arg(stateText)
        .arg(sectionTitle)
        .arg(card)
        .arg(scrollArea)
        .arg(scrollbar)
        .arg(footer)
        .arg(middleColumn)
        .arg(columnSeparator)
        .arg(portCard)
        .arg(outputCard);
}

void NodeDetailWindow::updatePreviewImage()
{
    if (_previewImagePaths.isEmpty() || _currentPreviewIndex < 0 || _currentPreviewIndex >= _previewImagePaths.size()) {
        if (_imageView && _imageView->scene()) {
            _imageView->scene()->clear();
        }
        if (_imageNameLabel) {
            _imageNameLabel->setText(QString());
        }
        if (_prevButton) _prevButton->setEnabled(false);
        if (_nextButton) _nextButton->setEnabled(false);
        return;
    }

    QString currentPath = _previewImagePaths[_currentPreviewIndex];
    if (QFileInfo::exists(currentPath)) {
        if (_imageView) {
            _imageView->loadImage(currentPath);
            _imageView->fitImage(); // Call fitImage to ensure it fits the view
        }
        
        QFileInfo fi(currentPath);
        if (_imageNameLabel) {
            QString fileName = fi.fileName();
            // Elide long filenames to prevent horizontal scrollbar in the Detail Window
            if (fileName.length() > 30) {
                fileName = fileName.left(12) + "..." + fileName.right(12);
            }
            _imageNameLabel->setText(QString("%1 (%2 / %3)")
                .arg(fileName)
                .arg(_currentPreviewIndex + 1)
                .arg(_previewImagePaths.size()));
        }
            
        if (_prevButton) _prevButton->setEnabled(_currentPreviewIndex > 0);
        if (_nextButton) _nextButton->setEnabled(_currentPreviewIndex < _previewImagePaths.size() - 1);
        
        // Sync table selection
        if (_resultsTable && _currentPreviewIndex < _resultsTable->rowCount()) {
            _resultsTable->blockSignals(true);
            _resultsTable->selectRow(_currentPreviewIndex);
            _resultsTable->blockSignals(false);
        }
    }
}

void NodeDetailWindow::fitPreviewImage()
{
    if (_imageView) {
        _imageView->fitImage();
    }
}

void NodeDetailWindow::onPrevPreviewClicked()
{
    if (_currentPreviewIndex > 0) {
        _currentPreviewIndex--;
        updatePreviewImage();
    }
}

void NodeDetailWindow::onNextPreviewClicked()
{
    if (_currentPreviewIndex < _previewImagePaths.size() - 1) {
        _currentPreviewIndex++;
        updatePreviewImage();
    }
}

void NodeDetailWindow::onTableSelectionChanged()
{
    if (_resultsTable) {
        int row = _resultsTable->currentRow();
        if (row >= 0 && row < _previewImagePaths.size()) {
            _currentPreviewIndex = row;
            updatePreviewImage();
        }
    }
}

void NodeDetailWindow::onMoveModeClicked()
{
    if (_roiEnableBtn && _roiEnableBtn->isChecked()) {
        _roiEnableBtn->blockSignals(true);
        _roiEnableBtn->setChecked(false);
        _roiEnableBtn->blockSignals(false);
    }
    if (_targetRoiBtn && _targetRoiBtn->isChecked()) {
        _targetRoiBtn->blockSignals(true);
        _targetRoiBtn->setChecked(false);
        _targetRoiBtn->blockSignals(false);
    }
    if (_clutterRoiBtn && _clutterRoiBtn->isChecked()) {
        _clutterRoiBtn->blockSignals(true);
        _clutterRoiBtn->setChecked(false);
        _clutterRoiBtn->blockSignals(false);
    }
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(true);
    }
    if (_imageView) {
        _imageView->setMoveMode();
    }
}

void NodeDetailWindow::onZoomInClicked()
{
    if (_imageView) {
        _imageView->zoomIn();
    }
}

void NodeDetailWindow::onZoomOutClicked()
{
    if (_imageView) {
        _imageView->zoomOut();
    }
}

void NodeDetailWindow::onFitImageClicked()
{
    fitPreviewImage();
}

void NodeDetailWindow::onRoiToggled(bool checked)
{
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(!checked);
    }
    if (_imageView) {
        _imageView->setRoiSelectionEnabled(checked);
    }
}

void NodeDetailWindow::onRoiCleared()
{
    if (_imageView) {
        _imageView->clearRoi();
        _imageView->setMoveMode();
    }
    if (_roiEnableBtn) {
        _roiEnableBtn->blockSignals(true);
        _roiEnableBtn->setChecked(false);
        _roiEnableBtn->blockSignals(false);
    }
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(true);
    }
    emit roiCleared();
}

void NodeDetailWindow::onTargetRoiToggled(bool checked)
{
    if (checked && _clutterRoiBtn && _clutterRoiBtn->isChecked()) {
        _clutterRoiBtn->blockSignals(true);
        _clutterRoiBtn->setChecked(false);
        _clutterRoiBtn->blockSignals(false);
    }
    
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(!checked);
    }

    if (_imageView && checked) {
        _imageView->setRoiSelectionMode(ImageView::RoiSelectionMode::Target);
    } else if (_imageView && !checked && _imageView->roiSelectionMode() == ImageView::RoiSelectionMode::Target) {
        _imageView->setRoiSelectionMode(ImageView::RoiSelectionMode::None);
    }
}

void NodeDetailWindow::onClutterRoiToggled(bool checked)
{
    if (checked && _targetRoiBtn && _targetRoiBtn->isChecked()) {
        _targetRoiBtn->blockSignals(true);
        _targetRoiBtn->setChecked(false);
        _targetRoiBtn->blockSignals(false);
    }
    
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(!checked);
    }

    if (_imageView && checked) {
        _imageView->setRoiSelectionMode(ImageView::RoiSelectionMode::Clutter);
    } else if (_imageView && !checked && _imageView->roiSelectionMode() == ImageView::RoiSelectionMode::Clutter) {
        _imageView->setRoiSelectionMode(ImageView::RoiSelectionMode::None);
    }
}

void NodeDetailWindow::onTargetRoiCleared()
{
    if (_imageView) {
        _imageView->clearTargetRoi();
        _imageView->setMoveMode();
    }
    if (_targetRoiBtn) {
        _targetRoiBtn->blockSignals(true);
        _targetRoiBtn->setChecked(false);
        _targetRoiBtn->blockSignals(false);
    }
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(true);
    }
    emit targetRoiCleared();
}

void NodeDetailWindow::onClutterRoiCleared()
{
    if (_imageView) {
        _imageView->clearClutterRoi();
        _imageView->setMoveMode();
    }
    if (_clutterRoiBtn) {
        _clutterRoiBtn->blockSignals(true);
        _clutterRoiBtn->setChecked(false);
        _clutterRoiBtn->blockSignals(false);
    }
    if (_moveModeBtn) {
        _moveModeBtn->setChecked(true);
    }
    emit clutterRoiCleared();
}

// ==========================================
// ElidedToolTipDelegate - Only show tooltip on overflow
// ==========================================
class ElidedToolTipDelegate : public QStyledItemDelegate
{
public:
    explicit ElidedToolTipDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        if (event && event->type() == QEvent::ToolTip) {
            QString text = index.data(Qt::DisplayRole).toString();
            QFontMetrics fm(option.font);
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
            int textWidth = fm.horizontalAdvance(text);
#else
            int textWidth = fm.width(text);
#endif
            int rectWidth = option.rect.width();

            // 如果文字显示宽度超过了单元格宽度（留8像素Padding间距），则悬浮显示Tooltip，否则隐藏
            if (textWidth > rectWidth - 8) {
                QToolTip::showText(event->globalPos(), text, view);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
};

// ==========================================
// ValidationComparisonTable Implementation
// ==========================================

ValidationComparisonTable::ValidationComparisonTable(QWidget* parent)
    : QTableWidget(parent)
{
    setItemDelegate(new ElidedToolTipDelegate(this));
    setColumnCount(4);
    QStringList headers;
    headers << QObject::tr("参数项") << QObject::tr("设置值") << QObject::tr("实际值") << QObject::tr("对比结论");
    setHorizontalHeaderLabels(headers);
    
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::NoSelection);
    
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    horizontalHeader()->setHighlightSections(false);
    verticalHeader()->setVisible(false);
    
    applyThemeStyle();
}

void ValidationComparisonTable::clearComparison()
{
    setRowCount(0);
}

void ValidationComparisonTable::addComparison(const QString& name, const QString& expected,
                                              const QString& actual, bool verifiable, const QString& tooltip)
{
    int row = rowCount();
    insertRow(row);
    
    auto* item0 = new QTableWidgetItem(name);
    if (!tooltip.isEmpty()) item0->setToolTip(tooltip);
    auto* item1 = new QTableWidgetItem(expected);
    auto* item2 = new QTableWidgetItem(actual);
    
    // Determine matching
    bool isMatch = false;
    if (verifiable) {
        QString expTrim = expected.trimmed();
        QString actTrim = actual.trimmed();
        if (expTrim == actTrim) {
            isMatch = true;
        } else {
            // Try numerical comparison
            bool ok1, ok2;
            double val1 = expTrim.toDouble(&ok1);
            double val2 = actTrim.toDouble(&ok2);
            if (ok1 && ok2 && std::abs(val1 - val2) < 1e-4) {
                isMatch = true;
            }
        }
    }
    
    QTableWidgetItem* item3 = nullptr;
    bool isDark = NodeDetailWindow::isDarkTheme(this);
    if (!verifiable) {
        // Actual value could not be extracted (e.g. older result without recorded
        // metadata): a neutral conclusion, not a red mismatch.
        item3 = new QTableWidgetItem(QObject::tr("未验证"));
        item3->setForeground(QBrush(QColor(isDark ? "#FBBF24" : "#B45309"))); // Amber
    } else if (isMatch) {
        item3 = new QTableWidgetItem(QObject::tr("一致"));
        item3->setForeground(QBrush(QColor(isDark ? "#34D399" : "#10B981"))); // Green
    } else {
        item3 = new QTableWidgetItem(QObject::tr("不一致"));
        item3->setForeground(QBrush(QColor(isDark ? "#F87171" : "#EF4444"))); // Red
    }
    
    // Stylize cells
    QString textColor = isDark ? "#D1D5DB" : "#374151";
    QBrush textBrush = QColor(textColor);
    item0->setForeground(textBrush);
    item1->setForeground(textBrush);
    item2->setForeground(textBrush);

    setItem(row, 0, item0);
    setItem(row, 1, item1);
    setItem(row, 2, item2);
    setItem(row, 3, item3);
}

void ValidationComparisonTable::addDiagnostic(const QString& name, const QString& value,
                                              const QString& tooltip, const QString& conclusion,
                                              bool conclusionIsWarning)
{
    const int row = rowCount();
    insertRow(row);

    auto* nameItem = new QTableWidgetItem(name);
    if (!tooltip.isEmpty()) nameItem->setToolTip(tooltip);
    auto* settingItem = new QTableWidgetItem(QStringLiteral("-"));
    auto* valueItem = new QTableWidgetItem(value);
    auto* conclusionItem = new QTableWidgetItem(conclusion.isEmpty()
        ? QObject::tr("诊断") : conclusion);

    const bool isDark = NodeDetailWindow::isDarkTheme(this);
    const QBrush textBrush = QColor(isDark ? "#D1D5DB" : "#374151");
    nameItem->setForeground(textBrush);
    settingItem->setForeground(textBrush);
    valueItem->setForeground(textBrush);
    if (conclusion.isEmpty()) {
        conclusionItem->setForeground(QBrush(QColor(isDark ? "#60A5FA" : "#2563EB")));
    } else if (conclusionIsWarning) {
        conclusionItem->setForeground(QBrush(QColor(isDark ? "#FBBF24" : "#B45309"))); // 琥珀 = 需复查
    } else {
        conclusionItem->setForeground(QBrush(QColor(isDark ? "#34D399" : "#10B981"))); // 绿 = 正常
    }

    setItem(row, 0, nameItem);
    setItem(row, 1, settingItem);
    setItem(row, 2, valueItem);
    setItem(row, 3, conclusionItem);
}

void ValidationComparisonTable::applyThemeStyle()
{
    bool isDark = NodeDetailWindow::isDarkTheme(this);
    
    // Modern elegant borderless header and rows style
    QString style = isDark ?
        "QTableWidget {"
        "  background-color: #1F2937;"
        "  alternate-background-color: #374151;"
        "  border: 1px solid #374151;"
        "  border-radius: 4px;"
        "  gridline-color: #374151;"
        "}"
        "QHeaderView::section {"
        "  background-color: #111827;"
        "  color: #9CA3AF;"
        "  padding: 6px;"
        "  border: none;"
        "  border-bottom: 1px solid #374151;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}" :
        "QTableWidget {"
        "  background-color: #FFFFFF;"
        "  alternate-background-color: #F9FAFB;"
        "  border: 1px solid #E5E7EB;"
        "  border-radius: 4px;"
        "  gridline-color: #E5E7EB;"
        "}"
        "QHeaderView::section {"
        "  background-color: #F3F4F6;"
        "  color: #4B5563;"
        "  padding: 6px;"
        "  border: none;"
        "  border-bottom: 1px solid #E5E7EB;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}";
        
    setStyleSheet(style);
    setAlternatingRowColors(true);
}

// ==========================================
// ValidationLoadingOverlay Implementation
// ==========================================

ValidationLoadingOverlay::ValidationLoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    hide();
    
    // Background overlay container (transparent/semi-transparent black)
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignCenter);
    
    _messageLabel = new QLabel(this);
    _messageLabel->setAlignment(Qt::AlignCenter);
    _messageLabel->setWordWrap(true);
    _messageLabel->setTextFormat(Qt::PlainText);
    _messageLabel->setMinimumWidth(0);
    _messageLabel->setMaximumWidth(520);
    _messageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(_messageLabel, 0, Qt::AlignHCenter);

    _retryButton = new QPushButton(QObject::tr("重试"), this);
    _retryButton->setFixedSize(80, 28);
    _retryButton->hide();
    layout->addWidget(_retryButton, 0, Qt::AlignHCenter);

    connect(_retryButton, &QPushButton::clicked, this, [this]() {
        _retryButton->hide();
        emit retryRequested();
    });

    _timeoutTimer = new QTimer(this);
    _timeoutTimer->setSingleShot(true);
    connect(_timeoutTimer, &QTimer::timeout, this, &ValidationLoadingOverlay::onTimeout);
}

void ValidationLoadingOverlay::startLoading(const QString& message, int timeoutMs)
{
    bool isDark = NodeDetailWindow::isDarkTheme(parentWidget());
    
    // Background overlay with dark tint and white/gray text
    QString overlayBg = isDark ? "rgba(17, 24, 39, 0.75)" : "rgba(255, 255, 255, 0.75)";
    QString textColor = isDark ? "#60A5FA" : "#2563EB"; // Blue text
    
    setStyleSheet(QString(
        "QWidget {"
        "  background-color: %1;"
        "}"
        "QLabel {"
        "  color: %2;"
        "  background-color: transparent;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton {"
        "  background-color: %3;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: %4;"
        "}"
    ).arg(overlayBg)
     .arg(textColor)
     .arg(isDark ? "#2563EB" : "#3B82F6")
     .arg(isDark ? "#1D4ED8" : "#2563EB"));
    
    _messageLabel->setText(message);
    _retryButton->hide();
    
    // Start timeout timer
    _timeoutTimer->stop();
    if (timeoutMs > 0) {
        _timeoutTimer->start(timeoutMs);
    }
    
    // Resize to parent immediately
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    
    show();
    raise();
}

void ValidationLoadingOverlay::stopLoading()
{
    _timeoutTimer->stop();
    _retryButton->hide();
    hide();
}

void ValidationLoadingOverlay::showTimeoutError(const QString& message)
{
    _timeoutTimer->stop();
    _messageLabel->setText(message);
    _messageLabel->setStyleSheet("color: #EF4444; font-size: 13px; font-weight: 600; background-color: transparent;");
    _retryButton->show();
    show();
    raise();
}

void ValidationLoadingOverlay::onTimeout()
{
    showTimeoutError(QObject::tr("计算超时，后台处理时间过长或发生挂起。请检查工程数据后重试。"));
    emit timeoutOccurred();
}

void ValidationLoadingOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
}

// ==========================================
// BaseValidationWidget Implementation
// ==========================================

BaseValidationWidget::BaseValidationWidget(ExecutableNodeDelegateModel* node, QWidget* parent)
    : QWidget(parent)
    , m_baseNode(node)
{
}

BaseValidationWidget::~BaseValidationWidget()
{
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
}

void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QString& initialDesc, const QString& featureTitleText,
    const QString& comparisonTitleText, bool stackContentVertically, bool scrollFeaturePanel)
{
    bool isDark = NodeDetailWindow::isDarkTheme(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    // 1. Status overview card
    m_statusCard = new QFrame(this);
    m_statusCard->setFrameShape(QFrame::StyledPanel);
    m_statusCard->setStyleSheet(isDark ?
        "QFrame { background-color: rgba(55, 65, 81, 0.4); border: 1px solid #374151; border-radius: 6px; padding: 12px; }" :
        "QFrame { background-color: rgba(243, 244, 246, 0.6); border: 1px solid #E5E7EB; border-radius: 6px; padding: 12px; }");

    QVBoxLayout* cardLayout = new QVBoxLayout(m_statusCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);

    m_statusTitle = new QLabel(initialTitle, m_statusCard);
    m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(isDark ? "#60A5FA" : "#2563EB"));
    m_statusDesc = new QLabel(initialDesc, m_statusCard);
    m_statusDesc->setWordWrap(true);
    m_statusDesc->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));

    cardLayout->addWidget(m_statusTitle);
    cardLayout->addWidget(m_statusDesc);
    mainLayout->addWidget(m_statusCard);

    // 2. Main content area. Most validators use columns; diagnostics-heavy pages can opt into stacking.
    QBoxLayout* contentLayout = stackContentVertically
        ? static_cast<QBoxLayout*>(new QVBoxLayout())
        : static_cast<QBoxLayout*>(new QHBoxLayout());
    contentLayout->setSpacing(12);

    // Left column: Parameter comparison table
    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(6);
    const QString tableTitleText = comparisonTitleText.isEmpty() ? QObject::tr("物理参数比对") : comparisonTitleText;
    QLabel* tableTitle = new QLabel(tableTitleText, this);
    tableTitle->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
    leftLayout->addWidget(tableTitle);

    m_compTable = new ValidationComparisonTable(this);
    m_compTable->setMinimumWidth(0);
    m_compTable->setMinimumHeight(0);
    m_compTable->setSizePolicy(QSizePolicy::Expanding,
        stackContentVertically ? QSizePolicy::Ignored : QSizePolicy::Expanding);
    leftLayout->addWidget(m_compTable, 1);
    contentLayout->addLayout(leftLayout, stackContentVertically ? 3 : 2);

    // Right column: Feature analysis panel
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(6);
    QLabel* fTitle = new QLabel(featureTitleText, this);
    fTitle->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
    rightLayout->addWidget(fTitle);

    m_featureCard = new QFrame(this);
    m_featureCard->setMinimumWidth(0);
    m_featureCard->setMinimumHeight(0);
    m_featureCard->setSizePolicy(stackContentVertically ? QSizePolicy::Expanding : QSizePolicy::Ignored,
        QSizePolicy::Preferred);
    m_featureCard->setStyleSheet(isDark ?
        "QFrame { background-color: #1F2937; border: 1px solid #374151; border-radius: 4px; padding: 12px; }" :
        "QFrame { background-color: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 4px; padding: 12px; }");
    
    m_featureLayout = new QFormLayout(m_featureCard);
    m_featureLayout->setContentsMargins(8, 8, 8, 8);
    m_featureLayout->setSpacing(10);
    m_featureLayout->setLabelAlignment(Qt::AlignRight);
    m_featureLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_featureLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    if (scrollFeaturePanel) {
        m_featureScrollArea = new QScrollArea(this);
        m_featureScrollArea->setWidgetResizable(true);
        m_featureScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_featureScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_featureScrollArea->setMinimumHeight(120);
        m_featureScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_featureScrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
        m_featureScrollArea->setWidget(m_featureCard);
        rightLayout->addWidget(m_featureScrollArea, 1);
    } else {
        rightLayout->addWidget(m_featureCard, 1);
    }
    contentLayout->addLayout(rightLayout, stackContentVertically ? 2 : 3);

    mainLayout->addLayout(contentLayout, 1);

    // 3. Loading Overlay
    m_loadingOverlay = new ValidationLoadingOverlay(this);
    
    connect(m_loadingOverlay, &ValidationLoadingOverlay::retryRequested, this, [this]() {
        m_isTimedOut = false;
        startAsyncValidation();
    });
    
    connect(m_loadingOverlay, &ValidationLoadingOverlay::timeoutOccurred, this, &BaseValidationWidget::onTimeout);
}

QGridLayout* BaseValidationWidget::replaceFeatureFormWithGrid()
{
    delete m_featureLayout;
    m_featureLayout = nullptr;

    QGridLayout* featureGrid = new QGridLayout(m_featureCard);
    featureGrid->setContentsMargins(8, 8, 8, 8);
    featureGrid->setHorizontalSpacing(24);
    featureGrid->setVerticalSpacing(12);
    featureGrid->setColumnStretch(0, 1);
    featureGrid->setColumnStretch(1, 1);
    return featureGrid;
}

QLabel* BaseValidationWidget::createFeatureLabel()
{
    bool isDark = NodeDetailWindow::isDarkTheme(this);
    QLabel* lbl = new QLabel(QObject::tr("正在计算..."), m_featureCard);
    lbl->setWordWrap(true);
    lbl->setMinimumWidth(0);
    lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    lbl->setStyleSheet(QString("font-weight: 600; color: %1;").arg(isDark ? "#F3F4F6" : "#111827"));
    return lbl;
}

QLabel* BaseValidationWidget::createHeaderLabel(const QString& text)
{
    bool isDark = NodeDetailWindow::isDarkTheme(this);
    QLabel* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setMinimumWidth(0);
    lbl->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    lbl->setStyleSheet(QString("color: %1; font-weight: 500;").arg(isDark ? "#9CA3AF" : "#4B5563"));
    return lbl;
}

void BaseValidationWidget::onTimeout()
{
    m_isTimedOut = true;
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    m_statusTitle->setText(QObject::tr("验证超时"));
    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
    m_statusDesc->setText(QObject::tr("验证计算超时，后台未响应。可能发生进程挂起或文件过大。"));
}

} // namespace QtNodes
