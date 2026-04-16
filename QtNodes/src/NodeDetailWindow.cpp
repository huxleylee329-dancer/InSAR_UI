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
#include <QDebug>

namespace QtNodes {

NodeDetailWindow::NodeDetailWindow(QWidget* parent)
    : QDialog(parent)
    , _closeButton(nullptr)
    , _titleLabel(nullptr)
    , _inputWidget(nullptr)
    , _processingWidget(nullptr)
    , _outputWidget(nullptr)
    , _inputLayout(nullptr)
    , _processingLayout(nullptr)
    , _outputLayout(nullptr)
{
    setModal(true);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // Don't use WA_TranslucentBackground as it interferes with opacity animation
    // setAttribute(Qt::WA_TranslucentBackground);

    // Set background color with alpha for transparency (theme-aware)
    setStyleSheet(getThemeStylesheet(parent));

    // Set initial opacity to very low but not zero to ensure window is "visible"
    setWindowOpacity(0.01);

    setupUI();
}

void NodeDetailWindow::setupUI()
{
    // Main layout
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title label
    _titleLabel = new QLabel();
    _titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = _titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    _titleLabel->setFont(titleFont);
    // No individual stylesheet - window-wide stylesheet is set in constructor
    mainLayout->addWidget(_titleLabel);

    // Content area with 3 columns
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(15);
    contentLayout->setContentsMargins(10, 0, 10, 0);

    // Create three sections
    _inputWidget = createInputSection();
    _processingWidget = createProcessingSection();
    _outputWidget = createOutputSection();

    contentLayout->addWidget(_inputWidget);
    contentLayout->addWidget(_processingWidget);
    contentLayout->addWidget(_outputWidget);

    mainLayout->addLayout(contentLayout);

    // Close button
    _closeButton = new QPushButton(QObject::tr("Close"));
    // No individual stylesheet - window-wide stylesheet is set in constructor
    connect(_closeButton, &QPushButton::clicked, this, &NodeDetailWindow::closeRequested);
    mainLayout->addWidget(_closeButton, 0, Qt::AlignCenter);

    setMinimumWidth(800);
    setMinimumHeight(500);
}

QWidget* NodeDetailWindow::createInputSection()
{
    auto* frame = new QFrame();
    frame->setObjectName("DetailCard");
    frame->setMinimumWidth(SECTION_MIN_WIDTH);
    // No individual stylesheet - window-wide stylesheet is set in constructor

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QObject::tr("Input Data"));
    title->setObjectName("CardTitle");
    // No individual stylesheet - window-wide stylesheet styles #CardTitle
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(CONTENT_MAX_HEIGHT);
    // No individual stylesheet - window-wide stylesheet handles scrollbar styling

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(STYLE_SCROLL_CONTENT);
    _inputLayout = new QVBoxLayout(scrollContent);
    _inputLayout->setContentsMargins(10, 10, 10, 10);
    _inputLayout->setSpacing(8);
    _inputLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    return frame;
}

QWidget* NodeDetailWindow::createProcessingSection()
{
    auto* frame = new QFrame();
    frame->setObjectName("DetailCard");
    frame->setMinimumWidth(SECTION_MIN_WIDTH);
    // No individual stylesheet - window-wide stylesheet is set in constructor

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QObject::tr("Processing Info"));
    title->setObjectName("CardTitle");
    // No individual stylesheet - window-wide stylesheet styles #CardTitle
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(CONTENT_MAX_HEIGHT);
    // No individual stylesheet - window-wide stylesheet handles scrollbar styling

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(STYLE_SCROLL_CONTENT);
    _processingLayout = new QVBoxLayout(scrollContent);
    _processingLayout->setContentsMargins(10, 10, 10, 10);
    _processingLayout->setSpacing(8);
    _processingLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    return frame;
}

QWidget* NodeDetailWindow::createOutputSection()
{
    auto* frame = new QFrame();
    frame->setObjectName("DetailCard");
    frame->setMinimumWidth(SECTION_MIN_WIDTH);
    // No individual stylesheet - window-wide stylesheet is set in constructor

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QObject::tr("Output Data"));
    title->setObjectName("CardTitle");
    // No individual stylesheet - window-wide stylesheet styles #CardTitle
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(CONTENT_MAX_HEIGHT);
    // No individual stylesheet - window-wide stylesheet handles scrollbar styling

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(STYLE_SCROLL_CONTENT);
    _outputLayout = new QVBoxLayout(scrollContent);
    _outputLayout->setContentsMargins(10, 10, 10, 10);
    _outputLayout->setSpacing(8);
    _outputLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    return frame;
}

void NodeDetailWindow::renderPortCard(QVBoxLayout* layout, const PortDataInfo& info, QWidget* parent)
{
    bool isDark = isDarkTheme(parent);

    // Create card with glass effect (same as PropertyEditor)
    QFrame* card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);

    QString cardBg = isDark ? "rgba(64, 64, 64, 0.5)" : "rgba(255, 255, 255, 0.7)";
    QString cardBorder = isDark ? "rgba(148, 163, 184, 0.3)" : "rgba(148, 163, 184, 0.3)";

    card->setStyleSheet(QString(
        "QFrame {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 8px;"
        "}"
    ).arg(cardBg).arg(cardBorder));

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(6);

    // Text colors based on theme (same as PropertyEditor)
    QString primaryTextColor = isDark ? "#FFFFFF" : "#1E3A8A";
    QString secondaryTextColor = isDark ? "#94A3B8" : "#64748B";
    QString tertiaryTextColor = isDark ? "#FFFFFF" : "#334155";

    // Port name with optional index (same as PropertyEditor)
    QString headerText;
    if (info.showIndex) {
        headerText = QString("<b>%1</b> <span style='color: %2;'>[%3]</span>")
            .arg(info.name).arg(secondaryTextColor).arg(info.index);
    } else {
        headerText = QString("<b>%1</b>").arg(info.name);
    }
    QLabel* nameLabel = new QLabel(headerText);
    nameLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(primaryTextColor));
    cardLayout->addWidget(nameLabel);

    // Data type (same as PropertyEditor)
    if (!info.dataType.isEmpty()) {
        QLabel* typeLabel = new QLabel(QString("Type: %1").arg(info.dataType));
        typeLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(secondaryTextColor));
        cardLayout->addWidget(typeLabel);
    }

    // Summary (same as PropertyEditor)
    if (!info.summary.isEmpty()) {
        QString displaySummary = info.summary;
        // Truncate long values
        if (displaySummary.length() > 60) {
            displaySummary = displaySummary.left(60) + "...";
        }

        QLabel* summaryLabel = new QLabel(displaySummary);
        QString summaryBg = isDark ? "rgba(64, 64, 64, 0.6)" : "rgba(241, 245, 249, 0.5)";
        summaryLabel->setStyleSheet(QString(
            "QLabel {"
            "   color: %1;"
            "   font-size: 11px;"
            "   background-color: %2;"
            "   padding: 6px 8px;"
            "   border-radius: 4px;"
            "   border-left: 3px solid #3B82F6;"
            "}"
        ).arg(tertiaryTextColor).arg(summaryBg));
        summaryLabel->setWordWrap(true);
        cardLayout->addWidget(summaryLabel);
    }

    // Fields (read-only only, same as PropertyEditor without edit support)
    if (!info.fields.isEmpty()) {
        for (const auto& field : info.fields) {
            QHBoxLayout* fieldLayout = new QHBoxLayout();
            fieldLayout->setSpacing(6);

            QLabel* keyLabel = new QLabel(field.key + ":");
            keyLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(secondaryTextColor));
            keyLabel->setMinimumWidth(60);
            fieldLayout->addWidget(keyLabel);

            QString displayValue = field.value;
            if (displayValue.length() > 50) {
                displayValue = displayValue.left(50) + "...";
            }
            QLabel* valueLabel = new QLabel(displayValue);
            valueLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(tertiaryTextColor));
            valueLabel->setWordWrap(true);
            fieldLayout->addWidget(valueLabel);

            cardLayout->addLayout(fieldLayout);
        }
    }

    // Connection status (simplified from PropertyEditor)
    if (!info.isConnected) {
        QLabel* statusLabel = new QLabel("Not connected");
        statusLabel->setStyleSheet(QString("color: %1; font-style: italic; font-size: 10px;")
            .arg(isDark ? "#EF4444" : "#AA0000"));
        cardLayout->addWidget(statusLabel);
    }

    layout->insertWidget(layout->count() - 1, card);
}

void NodeDetailWindow::renderParameterCard(QVBoxLayout* layout, const ParameterInfo& param, QWidget* parent)
{
    bool isDark = isDarkTheme(parent);

    // Create card with glass effect (same as PropertyEditor)
    QFrame* card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);

    QString cardBg = isDark ? "rgba(64, 64, 64, 0.5)" : "rgba(255, 255, 255, 0.7)";
    QString cardBorder = isDark ? "rgba(148, 163, 184, 0.3)" : "rgba(148, 163, 184, 0.3)";

    card->setStyleSheet(QString(
        "QFrame {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 8px;"
        "}"
    ).arg(cardBg).arg(cardBorder));

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(6);

    // Text colors based on theme (same as PropertyEditor)
    QString primaryTextColor = isDark ? "#FFFFFF" : "#1E3A8A";
    QString secondaryTextColor = isDark ? "#94A3B8" : "#64748B";
    QString tertiaryTextColor = isDark ? "#FFFFFF" : "#334155";

    // Parameter name with bold styling
    QLabel* nameLabel = new QLabel(QString("<b>%1</b>").arg(param.name));
    nameLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(primaryTextColor));
    cardLayout->addWidget(nameLabel);

    // Data type
    QLabel* typeLabel = new QLabel(QString("Type: %1").arg(param.dataType));
    typeLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(secondaryTextColor));
    cardLayout->addWidget(typeLabel);

    // Display value (read-only in Detail View, same as PropertyEditor's read-only)
    QString displayValue = param.value;
    if (displayValue.length() > 60) {
        displayValue = displayValue.left(60) + "...";
    }

    QLabel* valueLabel = new QLabel(displayValue);
    QString valueBg = isDark ? "rgba(64, 64, 64, 0.6)" : "rgba(241, 245, 249, 0.5)";
    valueLabel->setStyleSheet(QString(
        "QLabel {"
        "   color: %1;"
        "   font-size: 11px;"
        "   background-color: %2;"
        "   padding: 6px 8px;"
        "   border-radius: 4px;"
        "   border-left: 3px solid #3B82F6;"
        "}"
    ).arg(tertiaryTextColor).arg(valueBg));
    valueLabel->setWordWrap(true);
    cardLayout->addWidget(valueLabel);

    layout->insertWidget(layout->count() - 1, card);
}

void NodeDetailWindow::loadData(const NodeDataSnapshot& snapshot)
{
    // Set title
    QString titleStr = QObject::tr("%1 (%2)")
        .arg(snapshot.nodeName)
        .arg(NodeDataSnapshot::stateToString(snapshot.state));
    _titleLabel->setText(titleStr);

    // Clear previous data
    clearData();

    // Store data
    _inputPorts = snapshot.inputPorts;
    _parameters = snapshot.parameters;
    _processingInfo = snapshot.processingInfo;
    _outputPorts = snapshot.outputPorts;

    // Add input port data and node parameters (same as PropertyEditor)
    bool hasContent = false;
    bool isDark = isDarkTheme(this);
    QString sectionTextColor = isDark ? "#94A3B8" : "#64748B";

    // Input ports first
    if (!_inputPorts.empty()) {
        QLabel* portsLabel = new QLabel(QObject::tr("Port Data"));
        portsLabel->setStyleSheet(QString("color: %1; font-weight: bold; margin-top: 8px; margin-bottom: 4px;").arg(sectionTextColor));
        _inputLayout->insertWidget(_inputLayout->count() - 1, portsLabel);

        for (size_t i = 0; i < _inputPorts.size(); ++i) {
            renderPortCard(_inputLayout, _inputPorts[i], this);
        }
        hasContent = true;
    }

    // Add node parameters if any (this is the missing part!)
    if (!_parameters.empty()) {
        if (hasContent) {
            // Add separator between ports and parameters
            QString sectionBgColor = isDark ? "rgba(64, 64, 64, 0.5)" : "rgba(241, 245, 249, 0.8)";
            QLabel* separator = new QLabel();
            separator->setStyleSheet(QString("background-color: %1; margin: 12px 0; max-height: 1px;").arg(sectionBgColor));
            _inputLayout->insertWidget(_inputLayout->count() - 1, separator);
        }

        QLabel* paramsLabel = new QLabel(QObject::tr("Node Parameters"));
        paramsLabel->setStyleSheet(QString("color: %1; font-weight: bold; margin-top: 8px; margin-bottom: 4px;").arg(sectionTextColor));
        _inputLayout->insertWidget(_inputLayout->count() - 1, paramsLabel);

        for (const auto& param : _parameters) {
            renderParameterCard(_inputLayout, param, this);
        }
        hasContent = true;
    }

    // Show "No content" message if nothing to display
    if (!hasContent) {
        bool isDark = isDarkTheme(this);
        QString textColor = isDark ? "#94A3B8" : "#94A3B8";
        QLabel* noContentLabel = new QLabel(QObject::tr("No input ports or parameters"));
        noContentLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(textColor));
        noContentLabel->setAlignment(Qt::AlignCenter);
        _inputLayout->insertWidget(_inputLayout->count() - 1, noContentLabel);
    }

    // Add processing info
    QString infoLabelTemplate = isDark ? STYLE_INFO_LABEL_TEMPLATE_DARK : STYLE_INFO_LABEL_TEMPLATE;
    if (_processingInfo.empty()) {
        QString textColor = isDark ? "#94A3B8" : "#94A3B8";
        QLabel* noContentLabel = new QLabel(QObject::tr("No processing info available"));
        noContentLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(textColor));
        noContentLabel->setAlignment(Qt::AlignCenter);
        _processingLayout->insertWidget(_processingLayout->count() - 1, noContentLabel);
    } else {
        for (size_t i = 0; i < _processingInfo.size(); ++i) {
            auto* infoLabel = new QLabel(_processingInfo[i]);
            infoLabel->setWordWrap(true);
            infoLabel->setStyleSheet(QString(infoLabelTemplate).arg(FONT_SIZE_INFO));
            _processingLayout->insertWidget(_processingLayout->count() - 1, infoLabel);
        }
    }

    // Add output port data
    if (_outputPorts.empty()) {
        bool isDark = isDarkTheme(this);
        QString textColor = isDark ? "#94A3B8" : "#94A3B8";
        QLabel* noContentLabel = new QLabel(QObject::tr("No output ports"));
        noContentLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(textColor));
        noContentLabel->setAlignment(Qt::AlignCenter);
        _outputLayout->insertWidget(_outputLayout->count() - 1, noContentLabel);
    } else {
        for (const auto& port : _outputPorts) {
            renderPortCard(_outputLayout, port, this);
        }
    }
}

void NodeDetailWindow::clearData()
{
    // Clear input section
    while (_inputLayout->count() > 1) {
        auto* item = _inputLayout->takeAt(_inputLayout->count() - 2);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    _inputPorts.clear();
    _parameters.clear();

    // Clear processing section
    while (_processingLayout->count() > 1) {
        auto* item = _processingLayout->takeAt(_processingLayout->count() - 2);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    _processingInfo.clear();

    // Clear output section
    while (_outputLayout->count() > 1) {
        auto* item = _outputLayout->takeAt(_outputLayout->count() - 2);
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

    // Card frame
    QString card = isDark ? STYLE_CARD_DARK : STYLE_CARD;

    // Card title
    QString cardTitle = isDark ? STYLE_CARD_TITLE_DARK : STYLE_CARD_TITLE;

    // Scroll area
    QString scrollArea = STYLE_SCROLL_AREA;  // Same for both themes

    // Info labels
    QString infoLabel = isDark ? STYLE_INFO_LABEL_TEMPLATE_DARK : STYLE_INFO_LABEL_TEMPLATE;

    // Close button
    QString closeButton = isDark ? STYLE_CLOSE_BUTTON_DARK : STYLE_CLOSE_BUTTON_LIGHT;

    // Scrollbar
    QString scrollbar = isDark ? STYLE_SCROLLBAR_DARK : STYLE_SCROLLBAR_LIGHT;

    // Title label
    QString title = isDark ? STYLE_TITLE_DARK : STYLE_TITLE_LIGHT;

    // Combine all styles
    return QString("%1%2%3%4%5%6%7%8%9")
        .arg(windowBg)
        .arg(card)
        .arg(cardTitle)
        .arg(scrollArea)
        .arg(infoLabel)
        .arg(closeButton)
        .arg(scrollbar)
        .arg(title);
}

} // namespace QtNodes
