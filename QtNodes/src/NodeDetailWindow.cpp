#include "QtNodes/internal/NodeDetailWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QFont>
#include <QVariant>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QStyle>
#include <QSize>
#include <QDebug>
#include <QFileInfo>
#include "ImageView.h"
#include "QtNodes/internal/StyleCollection.hpp"

namespace QtNodes {

NodeDetailWindow::NodeDetailWindow(QWidget* parent)
    : QDialog(parent)
    , _closeButton(nullptr)
    , _titleCloseButton(nullptr)
    , _titleIcon(nullptr)
    , _titleText(nullptr)
    , _titleState(nullptr)
    , _titleBarWidget(nullptr)
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

    // Content area with 3 columns
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(16);
    contentLayout->setContentsMargins(16, 16, 16, 16);

    // Create three sections with separators
    _inputWidget = createInputSection();
    contentLayout->addWidget(_inputWidget, 0, Qt::AlignTop);  // 顶部对齐

    contentLayout->addWidget(createColumnSeparator());

    _processingWidget = createProcessingSection();
    contentLayout->addWidget(_processingWidget, 1);  // 中间列stretch填充

    contentLayout->addWidget(createColumnSeparator());

    _outputWidget = createOutputSection();
    contentLayout->addWidget(_outputWidget, 0, Qt::AlignTop);  // 顶部对齐

    mainLayout->addLayout(contentLayout);

    // Footer
    _footerWidget = createFooter();
    mainLayout->addWidget(_footerWidget);

    setMinimumWidth(900);
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
    container->setMinimumWidth(SECTION_MIN_WIDTH);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);  // 不扩展

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel(QObject::tr("Input Data"));
    title->setStyleSheet(isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
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
    container->setMinimumWidth(SECTION_MIN_WIDTH);
    container->setStyleSheet(isDark ? STYLE_MIDDLE_COLUMN_DARK : STYLE_MIDDLE_COLUMN_LIGHT);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel(QObject::tr("Processing Info"));
    title->setStyleSheet(isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
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
    container->setMinimumWidth(SECTION_MIN_WIDTH);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);  // 不扩展

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Section title
    auto* title = new QLabel(QObject::tr("Output Data"));
    title->setStyleSheet(isDark ? STYLE_SECTION_TITLE_DARK : STYLE_SECTION_TITLE_LIGHT);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
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
        QString connectorColor = isDark ? "#6B7280" : "#94A3B8";
        QString connectorBorder = isDark ? "#4B5563" : "#64748B";
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
    QString indexColor = isOutput ? (isDark ? "#60A5FA" : "#3B82F6") : secondaryTextColor;
    QString portNameColor = isOutput ? (isDark ? "#60A5FA" : "#1D4ED8") : primaryTextColor;

    // Header row: Port name + Status badge
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);

    // Port name with optional index
    QString headerText;
    if (info.showIndex) {
        headerText = QString("<span style='font-weight: %1; font-size: 12px; color: %2; text-transform: none;'>%3</span> "
                             "<span style='color: %4; font-size: 11px;'>[%5]</span>")
            .arg(isOutput ? "700" : "600").arg(portNameColor).arg(info.name).arg(indexColor).arg(info.index);
    } else {
        headerText = QString("<span style='font-weight: %1; font-size: 12px; color: %2; text-transform: none;'>%3</span>")
            .arg(isOutput ? "700" : "600").arg(portNameColor).arg(info.name);
    }
    QLabel* nameLabel = new QLabel(headerText);
    headerLayout->addWidget(nameLabel);

    headerLayout->addStretch();

    // Status badge
    bool hasData = !info.summary.isEmpty() || info.isConnected;
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

    // Summary (preview area)
    if (!info.summary.isEmpty()) {
        QString displaySummary = info.summary;
        if (displaySummary.length() > 60) {
            displaySummary = displaySummary.left(60) + "...";
        }

        // Preview section with label (HTML design has separate label)
        QWidget* previewContainer = new QWidget();
        QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
        previewLayout->setContentsMargins(0, 0, 0, 0);
        previewLayout->setSpacing(4);

        // Preview label
        QLabel* previewLabel = new QLabel(QObject::tr("Preview"));
        previewLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 500; text-transform: none;").arg(secondaryTextColor));
        previewLayout->addWidget(previewLabel);

        // Preview value
        QLabel* valueLabel = new QLabel(displaySummary);
        QString previewBg = isDark ? "#1F2937" : "#F9FAFB";
        QString previewBorder = isDark ? "#374151" : "#E5E7EB";
        valueLabel->setStyleSheet(QString(
            "QLabel {"
            "   color: %1;"
            "   font-size: 12px;"
            "   font-family: monospace;"
            "   background-color: %2;"
            "   border: 1px solid %3;"
            "   padding: 5px 6px;"
            "   border-radius: 4px;"
            "}"
        ).arg(tertiaryTextColor).arg(previewBg).arg(previewBorder));
        valueLabel->setWordWrap(true);
        previewLayout->addWidget(valueLabel);

        cardLayout->addWidget(previewContainer);
    }

    // Fields section
    if (!info.fields.isEmpty()) {
        // Separator
        QFrame* separator = new QFrame();
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet(QString("background-color: %1;").arg(isDark ? "#374151" : "#E5E7EB"));
        separator->setFixedHeight(1);
        cardLayout->addWidget(separator);

        for (const auto& field : info.fields) {
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
        QLabel* statusLabel = new QLabel(QObject::tr("Not connected"));
        statusLabel->setStyleSheet(QString("color: %1; font-style: italic; font-size: 11px; text-transform: none;")
            .arg(isDark ? "#F87171" : "#DC2626"));
        cardLayout->addWidget(statusLabel);
    }

    containerLayout->addWidget(card);

    // Output connector dot (right side) - align to center to match HTML design
    if (isOutput) {
        QWidget* connector = new QWidget();
        connector->setFixedSize(8, 8);
        connector->setStyleSheet(
            "background-color: #3B82F6; border: 1px solid #2563EB; border-radius: 4px;"
        );

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

void NodeDetailWindow::loadData(const NodeDataSnapshot& snapshot)
{
    bool isDark = isDarkTheme(this);

    // Set title text and state
    _titleText->setText(snapshot.nodeName);
    _titleState->setText(QString("(%1)").arg(NodeDataSnapshot::stateToString(snapshot.state)));

    // Clear previous data
    clearData();

    // Store data
    _inputPorts = snapshot.inputPorts;
    _parameters = snapshot.parameters;
    _processingInfo = snapshot.processingInfo;
    _outputPorts = snapshot.outputPorts;

    // ===== INPUT SECTION: Port Data + Node Parameters =====
    bool hasInputContent = false;
    QString sectionTextColor = isDark ? "#9CA3AF" : "#6B7280";

    // Input ports first
    if (!_inputPorts.empty()) {
        QLabel* portsLabel = new QLabel(QObject::tr("Port Data"));
        portsLabel->setStyleSheet(QString(
            "color: %1; font-weight: bold; font-size: 11px; margin-bottom: 4px;"
        ).arg(sectionTextColor));
        _inputLayout->addWidget( portsLabel);

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

    // ===== PROCESSING INFO SECTION =====
    QString infoLabelTemplate = isDark ? STYLE_INFO_LABEL_TEMPLATE_DARK : STYLE_INFO_LABEL_TEMPLATE;
    
    _previewImagePaths = snapshot.previewImagePaths;
    _detectionResults = snapshot.detectionResults;
    _currentPreviewIndex = 0;
    
    bool hasPreviewImage = !_previewImagePaths.isEmpty() && QFileInfo::exists(_previewImagePaths.first());
    
    if (hasPreviewImage) {
        _imageView = new ImageView();
        _imageView->setMinimumHeight(300); // Ensure the image has some vertical space
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
        
        bool hasDetectionResults = !_detectionResults.isEmpty() && _detectionResults.size() == _previewImagePaths.size();
        
        if (hasDetectionResults) {
            // Add overlay label above the navigation layout (under the image)
            _imageOverlayLabel = new QLabel();
            _imageOverlayLabel->setAlignment(Qt::AlignCenter);
            _imageOverlayLabel->setWordWrap(true);
            
            QString overlayStyle = isDark ? 
                "QLabel { background-color: #374151; color: #F9FAFB; border-radius: 6px; padding: 6px; font-size: 14px; margin: 4px 0px; }" :
                "QLabel { background-color: #F3F4F6; color: #111827; border-radius: 6px; padding: 6px; font-size: 14px; margin: 4px 0px; border: 1px solid #E5E7EB; }";
            _imageOverlayLabel->setStyleSheet(overlayStyle);
            
            _processingLayout->insertWidget(_processingLayout->indexOf(_imageView) + 1, _imageOverlayLabel);
            
            // Add results table
            _resultsTable = new QTableWidget();
            _resultsTable->setColumnCount(3);
            _resultsTable->setHorizontalHeaderLabels({"文件名", "检测结果", "置信度"});
            _resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            _resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            _resultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            _resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            _resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
            _resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            _resultsTable->verticalHeader()->setVisible(false);
            _resultsTable->setAlternatingRowColors(true);
            _resultsTable->setShowGrid(false);
            
            QString tableStyle = isDark ? 
                "QTableWidget { background-color: #1F2937; alternate-background-color: #374151; border: 1px solid #4B5563; border-radius: 4px; color: #F3F4F6; }"
                "QTableWidget::item { padding: 4px 8px; }"
                "QTableWidget::item:selected { background-color: #3B82F6; color: white; }"
                "QHeaderView::section { background-color: #111827; padding: 6px; border: none; border-bottom: 1px solid #4B5563; font-weight: bold; color: #D1D5DB; font-size: 13px; }" :
                "QTableWidget { background-color: #FFFFFF; alternate-background-color: #F9FAFB; border: 1px solid #E5E7EB; border-radius: 4px; color: #1F2937; }"
                "QTableWidget::item { padding: 4px 8px; }"
                "QTableWidget::item:selected { background-color: #EFF6FF; color: #1D4ED8; }"
                "QHeaderView::section { background-color: #F3F4F6; padding: 6px; border: none; border-bottom: 1px solid #E5E7EB; font-weight: bold; color: #4B5563; font-size: 13px; }";
            _resultsTable->setStyleSheet(tableStyle);
            
            // Populate table
            _resultsTable->setRowCount(_detectionResults.size());
            for (int i = 0; i < _detectionResults.size(); ++i) {
                const auto& rowData = _detectionResults[i];
                if (rowData.size() >= 3) {
                    _resultsTable->setItem(i, 0, new QTableWidgetItem(rowData[0]));
                    
                    auto* resultItem = new QTableWidgetItem(rowData[1]);
                    resultItem->setTextAlignment(Qt::AlignCenter);
                    if (rowData[1].toLower() == "ship") {
                        resultItem->setForeground(QBrush(QColor(isDark ? "#34D399" : "#10B981"))); // Green
                    }
                    _resultsTable->setItem(i, 1, resultItem);
                    
                    auto* probItem = new QTableWidgetItem(rowData[2]);
                    probItem->setTextAlignment(Qt::AlignCenter);
                    _resultsTable->setItem(i, 2, probItem);
                }
            }
            
            connect(_resultsTable, &QTableWidget::itemSelectionChanged, this, &NodeDetailWindow::onTableSelectionChanged);
            _processingLayout->addWidget(_resultsTable);
            
            // Adjust proportions
            _imageView->setMinimumHeight(250);
            _resultsTable->setMinimumHeight(200);
        }
        
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
        for (size_t i = 0; i < _processingInfo.size(); ++i) {
            auto* infoLabel = new QLabel(_processingInfo[i]);
            infoLabel->setWordWrap(true);
            infoLabel->setStyleSheet(QString(infoLabelTemplate).arg(11));
            _processingLayout->addWidget( infoLabel);
        }
    }

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

}

void NodeDetailWindow::clearData()
{
    // Clear input section
    while (_inputLayout->count() > 0) {
        auto* item = _inputLayout->takeAt(_inputLayout->count() - 1);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    _inputPorts.clear();
    _parameters.clear();

    // Clear processing section
    while (_processingLayout->count() > 0) {
        auto* item = _processingLayout->takeAt(_processingLayout->count() - 1);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    _processingInfo.clear();

    // Clear output section (widgets + stretch spacer)
    while (_outputLayout->count() > 0) {
        auto* item = _outputLayout->takeAt(_outputLayout->count() - 1);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    _outputPorts.clear();
}

// ============================================================================
// Theme Detection Helper Functions
// ============================================================================

bool NodeDetailWindow::isDarkTheme(QWidget* parent)
{
    if (!parent) return false;

    // Check parent window's background color
    QVariant bgColor = parent->property("theme-background");
    if (bgColor.isValid()) {
        QColor color = bgColor.value<QColor>();
        // Dark theme: dark background colors
        if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
            return true;
        }
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
    return QString("%1%2%3%4%5%6%7%8%9%10%11%12")
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
    if (_previewImagePaths.isEmpty() || _currentPreviewIndex < 0 || _currentPreviewIndex >= _previewImagePaths.size())
        return;

    QString currentPath = _previewImagePaths[_currentPreviewIndex];
    if (QFileInfo::exists(currentPath)) {
        QGraphicsScene* scene = _imageView->scene();
        if (!scene) {
            scene = new QGraphicsScene(_imageView);
            _imageView->setScene(scene);
        } else {
            scene->clear();
        }
        
        QPixmap pixmap(currentPath);
        scene->addPixmap(pixmap);
        
        QFileInfo fi(currentPath);
        _imageNameLabel->setText(QString("%1 (%2 / %3)")
            .arg(fi.fileName())
            .arg(_currentPreviewIndex + 1)
            .arg(_previewImagePaths.size()));
            
        _prevButton->setEnabled(_currentPreviewIndex > 0);
        _nextButton->setEnabled(_currentPreviewIndex < _previewImagePaths.size() - 1);
        
        // Update overlay and sync table
        if (_imageOverlayLabel && _resultsTable && _currentPreviewIndex < _detectionResults.size()) {
            const auto& rowData = _detectionResults[_currentPreviewIndex];
            if (rowData.size() >= 3) {
                QString result = rowData[1];
                QString prob = rowData[2];
                bool isDark = isDarkTheme(this);
                QString color = (result.toLower() == "ship") ? (isDark ? "#34D399" : "#10B981") : (isDark ? "#F87171" : "#EF4444"); // Green for ship, red for no ship
                
                _imageOverlayLabel->setText(QString(
                    "检测结果: <b style='color: %1;'>%2</b> &nbsp;&nbsp;|&nbsp;&nbsp; 置信度: <b>%3</b>"
                ).arg(color, result, prob));
                
                _resultsTable->blockSignals(true);
                _resultsTable->selectRow(_currentPreviewIndex);
                _resultsTable->blockSignals(false);
            }
        }
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

} // namespace QtNodes
