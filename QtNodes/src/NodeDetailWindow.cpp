#include "QtNodes/internal/NodeDetailWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
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
    contentLayout->addWidget(_inputWidget);

    _leftSeparator = createColumnSeparator();
    contentLayout->addWidget(_leftSeparator);

    _processingWidget = createProcessingSection();
    contentLayout->addWidget(_processingWidget, 1);  // 中间列stretch填充

    _rightSeparator = createColumnSeparator();
    contentLayout->addWidget(_rightSeparator);

    _outputWidget = createOutputSection();
    contentLayout->addWidget(_outputWidget);

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
        if (displaySummary.length() > 60) {
            displaySummary = displaySummary.left(60) + "...";
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
                "font-size: 13px;"
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
                QStringList pathLines = pathValue.split("\n", Qt::SkipEmptyParts);
                QString displayPath;
                for (int i = 0; i < pathLines.size(); ++i) {
                    QString displayLine = pathLines[i];
                    if (displayLine.length() > 60) {
                        displayLine = displayLine.left(25) + "..." + displayLine.right(30);
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
        _imageView->setMinimumHeight(300); // Ensure the image has some vertical space
        _processingLayout->addWidget(_imageView);
        
        // View will be loaded at the end of section creation
        
        bool isDark = isDarkTheme(this);
        QString toggleBtnStyle = QString(
            "QPushButton {"
            "  padding: 4px 10px;"
            "  border: 1px solid %1;"
            "  border-radius: 4px;"
            "  background-color: %2;"
            "  color: %3;"
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
            "  padding: 4px 8px;"
            "  border: 1px solid %1;"
            "  border-radius: 4px;"
            "  background-color: %2;"
            "  color: #EF4444;"
            "}"
            "QPushButton:hover {"
            "  background-color: %3;"
            "}")
            .arg(isDark ? "#4B5563" : "#D1D5DB")
            .arg(isDark ? "#1F2937" : "#FFFFFF")
            .arg(isDark ? "#7F1D1D" : "#FEE2E2");

        if (_supportsTwoRois) {
            _roiToolbar = new QWidget();
            auto* roiLayout = new QHBoxLayout(_roiToolbar);
            roiLayout->setContentsMargins(5, 5, 5, 5);
            roiLayout->setSpacing(5);
            
            _targetRoiBtn = new QPushButton(QStringLiteral("绘制目标区域"));
            _targetRoiBtn->setCheckable(true);
            _targetRoiBtn->setStyleSheet(toggleBtnStyle);
            
            _clutterRoiBtn = new QPushButton(QStringLiteral("绘制杂波区域"));
            _clutterRoiBtn->setCheckable(true);
            _clutterRoiBtn->setStyleSheet(toggleBtnStyle);
            
            if (_hasTargetRoi) _imageView->setTargetRoiRect(_targetRoi);
            if (_hasClutterRoi) _imageView->setClutterRoiRect(_clutterRoi);
            
            connect(_targetRoiBtn, &QPushButton::toggled, this, &NodeDetailWindow::onTargetRoiToggled);
            connect(_clutterRoiBtn, &QPushButton::toggled, this, &NodeDetailWindow::onClutterRoiToggled);
            
            auto* clearTargetBtn = new QPushButton(QStringLiteral("✖ 清除"));
            clearTargetBtn->setStyleSheet(clearBtnStyle);
            clearTargetBtn->setFixedWidth(60);
            
            auto* clearClutterBtn = new QPushButton(QStringLiteral("✖ 清除"));
            clearClutterBtn->setStyleSheet(clearBtnStyle);
            clearClutterBtn->setFixedWidth(60);
            
            connect(clearTargetBtn, &QPushButton::clicked, this, &NodeDetailWindow::onTargetRoiCleared);
            connect(clearClutterBtn, &QPushButton::clicked, this, &NodeDetailWindow::onClutterRoiCleared);
            
            roiLayout->addWidget(_targetRoiBtn);
            roiLayout->addWidget(clearTargetBtn);
            
            QFrame* vLine = new QFrame();
            vLine->setFrameShape(QFrame::VLine);
            vLine->setFrameShadow(QFrame::Sunken);
            vLine->setStyleSheet(isDark ? "background-color: #4B5563;" : "background-color: #D1D5DB;");
            roiLayout->addWidget(vLine);
            
            roiLayout->addWidget(_clutterRoiBtn);
            roiLayout->addWidget(clearClutterBtn);
            
            roiLayout->addStretch();
            
            _processingLayout->addWidget(_roiToolbar);
            
            connect(_imageView, &ImageView::targetRoiSelected, this, [this](const QRectF& rect) {
                emit targetRoiSelectionChanged(rect, _currentPreviewIndex);
            });
            connect(_imageView, &ImageView::clutterRoiSelected, this, [this](const QRectF& rect) {
                emit clutterRoiSelectionChanged(rect, _currentPreviewIndex);
            });
        }
        else if (_supportsRoiSelection) {
            _roiToolbar = new QWidget();
            auto* roiLayout = new QHBoxLayout(_roiToolbar);
            roiLayout->setContentsMargins(5, 5, 5, 5);
            
            _roiEnableBtn = new QPushButton(QStringLiteral("绘制框选区域"));
            _roiEnableBtn->setCheckable(true);
            _roiEnableBtn->setStyleSheet(toggleBtnStyle);
            _roiEnableBtn->setChecked(_hasCustomRoi);
            
            _imageView->setRoiSelectionEnabled(_hasCustomRoi);
            if (_hasCustomRoi) {
                _imageView->setRoiRect(_customRoi);
            }
            
            connect(_roiEnableBtn, &QPushButton::toggled, this, &NodeDetailWindow::onRoiToggled);
            
            auto* clearRoiBtn = new QPushButton(QStringLiteral("✖ 清除"));
            clearRoiBtn->setStyleSheet(clearBtnStyle);
            clearRoiBtn->setFixedWidth(60);
            connect(clearRoiBtn, &QPushButton::clicked, this, &NodeDetailWindow::onRoiCleared);
            
            roiLayout->addWidget(_roiEnableBtn);
            roiLayout->addWidget(clearRoiBtn);
            roiLayout->addStretch();
            
            _processingLayout->addWidget(_roiToolbar);
            
            connect(_imageView, &ImageView::roiSelected, this, [this](const QRectF& rect) {
                emit roiSelectionChanged(rect, _currentPreviewIndex);
            });
        }
        
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
        for (size_t i = 0; i < _processingInfo.size(); ++i) {
            auto* infoLabel = new QLabel(_processingInfo[i]);
            infoLabel->setWordWrap(true);
            infoLabel->setStyleSheet(QString(infoLabelTemplate).arg(11));
            _processingLayout->addWidget( infoLabel);
        }
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

}

void NodeDetailWindow::updateTableData(const NodeDataSnapshot& snapshot)
{
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
    if (_previewImagePaths.isEmpty() || _currentPreviewIndex < 0 || _currentPreviewIndex >= _previewImagePaths.size())
        return;

    QString currentPath = _previewImagePaths[_currentPreviewIndex];
    if (QFileInfo::exists(currentPath)) {
        if (_imageView) {
            _imageView->loadImage(currentPath);
        }
        
        QFileInfo fi(currentPath);
        if (_imageNameLabel) {
            _imageNameLabel->setText(QString("%1 (%2 / %3)")
                .arg(fi.fileName())
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

void NodeDetailWindow::onRoiToggled(bool checked)
{
    if (_imageView) {
        _imageView->setRoiSelectionEnabled(checked);
    }
}

void NodeDetailWindow::onRoiCleared()
{
    if (_imageView) {
        _imageView->clearRoi();
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
    }
    emit targetRoiCleared();
}

void NodeDetailWindow::onClutterRoiCleared()
{
    if (_imageView) {
        _imageView->clearClutterRoi();
    }
    emit clutterRoiCleared();
}

} // namespace QtNodes
