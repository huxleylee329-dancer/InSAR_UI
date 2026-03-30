#include "QtNodes/internal/NodeDetailWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QFont>

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

    // Set background color with alpha for transparency
    setStyleSheet(STYLE_WINDOW);

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
    _titleLabel->setStyleSheet(STYLE_TITLE);
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
    _closeButton->setStyleSheet(STYLE_CLOSE_BUTTON);
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
    frame->setStyleSheet(STYLE_CARD);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QObject::tr("Input Data"));
    title->setObjectName("CardTitle");
    title->setStyleSheet(STYLE_CARD_TITLE);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(CONTENT_MAX_HEIGHT);
    scrollArea->setStyleSheet(QString(STYLE_SCROLL_AREA) + " " + STYLE_SCROLLBAR);

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
    frame->setStyleSheet(STYLE_CARD);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QObject::tr("Processing Info"));
    title->setObjectName("CardTitle");
    title->setStyleSheet(STYLE_CARD_TITLE);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(CONTENT_MAX_HEIGHT);
    scrollArea->setStyleSheet(QString(STYLE_SCROLL_AREA) + " " + STYLE_SCROLLBAR);

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
    frame->setStyleSheet(STYLE_CARD);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QObject::tr("Output Data"));
    title->setObjectName("CardTitle");
    title->setStyleSheet(STYLE_CARD_TITLE);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(CONTENT_MAX_HEIGHT);
    scrollArea->setStyleSheet(QString(STYLE_SCROLL_AREA) + " " + STYLE_SCROLLBAR);

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

void NodeDetailWindow::addPortData(QVBoxLayout* layout, const PortDataInfo& info, const QString& title)
{
    // Port info card frame
    auto* cardFrame = new QFrame();
    cardFrame->setStyleSheet(STYLE_PORT_CARD);
    auto* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(6);

    auto* nameLabel = new QLabel(title);
    nameLabel->setStyleSheet(QString("color: #333333; font-weight: bold; font-size: %1px;").arg(FONT_SIZE_PORT_NAME));
    cardLayout->addWidget(nameLabel);

    auto* typeLabel = new QLabel(QObject::tr("Type: %1").arg(info.dataType));
    typeLabel->setWordWrap(true);
    typeLabel->setStyleSheet(QString("color: #666666; font-size: %1px;").arg(FONT_SIZE_PORT_TYPE));
    cardLayout->addWidget(typeLabel);

    auto* valueLabel = new QLabel(QObject::tr("Value: %1").arg(info.value));
    valueLabel->setWordWrap(true);
    valueLabel->setStyleSheet(QString("color: #333333; font-weight: bold; font-size: %1px;").arg(FONT_SIZE_PORT_VALUE));
    cardLayout->addWidget(valueLabel);

    auto* statusLabel = new QLabel(info.isConnected
        ? QObject::tr("Connected")
        : QObject::tr("Not connected"));
    statusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
        .arg(info.isConnected ? "#00A000" : "#AA0000")
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
        addPortData(_inputLayout, port, portTitle);
    }

    // Add processing info
    for (size_t i = 0; i < _processingInfo.size(); ++i) {
        auto* infoLabel = new QLabel(_processingInfo[i]);
        infoLabel->setWordWrap(true);
        infoLabel->setStyleSheet(QString(STYLE_INFO_LABEL_TEMPLATE).arg(FONT_SIZE_INFO));
        _processingLayout->insertWidget(_processingLayout->count() - 1, infoLabel);
    }

    // Add output port data
    for (const auto& port : _outputPorts) {
        QString portTitle = QObject::tr("Port %1: %2")
            .arg(port.index)
            .arg(port.name);
        addPortData(_outputLayout, port, portTitle);
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

} // namespace QtNodes
