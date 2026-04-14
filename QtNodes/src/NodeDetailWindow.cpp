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
    // No individual stylesheet on scroll content
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
    // No individual stylesheet on scroll content
    _outputLayout = new QVBoxLayout(scrollContent);
    _outputLayout->setContentsMargins(10, 10, 10, 10);
    _outputLayout->setSpacing(8);
    _outputLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    return frame;
}

void NodeDetailWindow::addPortData(QVBoxLayout* layout, const PortDataInfo& info, const QString& title, QWidget* parent)
{
    bool isDark = isDarkTheme(parent);

    // Port info card frame
    auto* cardFrame = new QFrame();
    cardFrame->setStyleSheet(isDark ? STYLE_PORT_CARD_DARK : STYLE_PORT_CARD);
    auto* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(6);

    // Port name label
    auto* nameLabel = new QLabel(title);
    nameLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: %2px;")
        .arg(isDark ? COLOR_PORT_NAME_DARK : COLOR_PORT_NAME_LIGHT)
        .arg(FONT_SIZE_PORT_NAME));
    cardLayout->addWidget(nameLabel);

    // Type label
    auto* typeLabel = new QLabel(QObject::tr("Type: %1").arg(info.dataType));
    typeLabel->setWordWrap(true);
    typeLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
        .arg(isDark ? COLOR_PORT_TYPE_DARK : COLOR_PORT_TYPE_LIGHT)
        .arg(FONT_SIZE_PORT_TYPE));
    cardLayout->addWidget(typeLabel);

    // Value label
    auto* valueLabel = new QLabel(QObject::tr("Value: %1").arg(info.value));
    valueLabel->setWordWrap(true);
    valueLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: %2px;")
        .arg(isDark ? COLOR_PORT_VALUE_DARK : COLOR_PORT_VALUE_LIGHT)
        .arg(FONT_SIZE_PORT_VALUE));
    cardLayout->addWidget(valueLabel);

    // Status label
    auto* statusLabel = new QLabel(info.isConnected
        ? QObject::tr("Connected")
        : QObject::tr("Not connected"));
    statusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
        .arg(info.isConnected
            ? (isDark ? COLOR_STATUS_CONNECTED_DARK : COLOR_STATUS_CONNECTED_LIGHT)
            : (isDark ? COLOR_STATUS_DISCONNECTED_DARK : COLOR_STATUS_DISCONNECTED_LIGHT))
        .arg(FONT_SIZE_PORT_TYPE));
    cardLayout->addWidget(statusLabel);

    layout->insertWidget(layout->count() - 1, cardFrame);
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
    _processingInfo = snapshot.processingInfo;
    _outputPorts = snapshot.outputPorts;

    // Add input port data
    for (const auto& port : _inputPorts) {
        QString portTitle = QObject::tr("Port %1: %2")
            .arg(port.index)
            .arg(port.name);
        addPortData(_inputLayout, port, portTitle, this);
    }

    // Add processing info
    bool isDark = isDarkTheme(parentWidget());
    QString infoLabelTemplate = isDark ? STYLE_INFO_LABEL_TEMPLATE_DARK : STYLE_INFO_LABEL_TEMPLATE;
    for (size_t i = 0; i < _processingInfo.size(); ++i) {
        auto* infoLabel = new QLabel(_processingInfo[i]);
        infoLabel->setWordWrap(true);
        infoLabel->setStyleSheet(QString(infoLabelTemplate).arg(FONT_SIZE_INFO));
        _processingLayout->insertWidget(_processingLayout->count() - 1, infoLabel);
    }

    // Add output port data
    for (const auto& port : _outputPorts) {
        QString portTitle = QObject::tr("Port %1: %2")
            .arg(port.index)
            .arg(port.name);
        addPortData(_outputLayout, port, portTitle, this);
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

    // Port card
    QString portCard = isDark ? STYLE_PORT_CARD_DARK : STYLE_PORT_CARD;

    // Info labels
    QString infoLabel = isDark ? STYLE_INFO_LABEL_TEMPLATE_DARK : STYLE_INFO_LABEL_TEMPLATE;

    // Text colors
    QString textPrimary = isDark ? STYLE_TEXT_PRIMARY_DARK : STYLE_TEXT_PRIMARY_LIGHT;
    QString textSecondary = isDark ? STYLE_TEXT_SECONDARY_DARK : STYLE_TEXT_SECONDARY_LIGHT;

    // Close button
    QString closeButton = isDark ? STYLE_CLOSE_BUTTON_DARK : STYLE_CLOSE_BUTTON_LIGHT;

    // Scrollbar
    QString scrollbar = isDark ? STYLE_SCROLLBAR_DARK : STYLE_SCROLLBAR_LIGHT;

    // Title label
    QString title = isDark ? STYLE_TITLE_DARK : STYLE_TITLE_LIGHT;

    // Combine all styles
    return QString("%1%2%3%4%5%6%7%8%9%10%11%12")
        .arg(windowBg)
        .arg(card)
        .arg(cardTitle)
        .arg(scrollArea)
        .arg(portCard)
        .arg(infoLabel)
        .arg(textPrimary)
        .arg(textSecondary)
        .arg(closeButton)
        .arg(scrollbar)
        .arg(title);
}

} // namespace QtNodes
