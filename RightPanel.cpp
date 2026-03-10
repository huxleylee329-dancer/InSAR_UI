#include "include/RightPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>
#include <QListWidget>



// ============================================================================
// PropertyEditor Implementation
// ============================================================================

PropertyEditor::PropertyEditor(QWidget *parent)
    : QWidget(parent)
    , m_scrollArea(nullptr)
    , m_contentWidget(nullptr)
    , m_formLayout(nullptr)
    , m_noSelectionLabel(nullptr)
    , m_nodeIdLabel(nullptr)
    , m_captionEdit(nullptr)
    , m_xSpinBox(nullptr)
    , m_ySpinBox(nullptr)
    , m_graphModel(nullptr)
    , m_currentNodeId(QtNodes::InvalidNodeId)
    , m_updatingProperties(false)
{
    setupUi();
}

PropertyEditor::~PropertyEditor()
{
    // Child widgets are automatically deleted by Qt
}

void PropertyEditor::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // No selection label (shown when no node is selected)
    m_noSelectionLabel = new QLabel("No node selected");
    m_noSelectionLabel->setAlignment(Qt::AlignCenter);
    m_noSelectionLabel->setStyleSheet("color: #888888; font-style: italic;");
    mainLayout->addWidget(m_noSelectionLabel);

    // Scroll area for properties
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->hide();

    m_contentWidget = new QWidget();
    m_formLayout = new QFormLayout(m_contentWidget);
    m_formLayout->setContentsMargins(8, 8, 8, 8);
    m_formLayout->setSpacing(6);

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);
}

void PropertyEditor::setGraphModel(QtNodes::DataFlowGraphModel *model)
{
    m_graphModel = model;
}

void PropertyEditor::setSelectedNode(QtNodes::NodeId nodeId)
{
    m_currentNodeId = nodeId;

    if (nodeId == QtNodes::InvalidNodeId || !m_graphModel)
    {
        clearSelection();
        return;
    }

    // Show scroll area, hide no selection label
    m_noSelectionLabel->hide();
    m_scrollArea->show();

    generateProperties(nodeId);
}

void PropertyEditor::clearSelection()
{
    m_currentNodeId = QtNodes::InvalidNodeId;
    m_noSelectionLabel->show();
    m_scrollArea->hide();
}

void PropertyEditor::clearProperties()
{
    // Remove all items from form layout
    while (m_formLayout->count() > 0)
    {
        QLayoutItem *item = m_formLayout->takeAt(0);
        if (item->widget())
        {
            delete item->widget();
        }
        delete item;
    }
    m_captionEdit = nullptr;
    m_nodeIdLabel = nullptr;
    m_xSpinBox = nullptr;
    m_ySpinBox = nullptr;
}

void PropertyEditor::generateProperties(QtNodes::NodeId nodeId)
{
    m_updatingProperties = true;
    clearProperties();

    if (!m_graphModel)
    {
        m_updatingProperties = false;
        return;
    }

    // Get node data
    QString caption = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Caption).toString();
    QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).toPointF();

    // Add Node ID (read-only)
    m_nodeIdLabel = new QLabel(QString::number(static_cast<quint64>(nodeId)));
    m_nodeIdLabel->setStyleSheet("color: #666666;");
    m_formLayout->addRow("Node ID:", m_nodeIdLabel);

    // Add Caption (editable)
    m_captionEdit = new QLineEdit(caption);
    m_captionEdit->setClearButtonEnabled(true);
    connect(m_captionEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_updatingProperties && m_currentNodeId != QtNodes::InvalidNodeId)
        {
            emit propertyChanged(m_currentNodeId, "caption", text);
        }
    });
    m_formLayout->addRow("Caption:", m_captionEdit);

    // Add Position X
    m_xSpinBox = new QDoubleSpinBox();
    m_xSpinBox->setRange(-999999.0, 999999.0);
    m_xSpinBox->setDecimals(1);
    m_xSpinBox->setValue(pos.x());
    connect(m_xSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updatingProperties && m_currentNodeId != QtNodes::InvalidNodeId)
        {
            QPointF pos = m_graphModel->nodeData(m_currentNodeId, QtNodes::NodeRole::Position).toPointF();
            pos.setX(value);
            emit propertyChanged(m_currentNodeId, "position", QVariant::fromValue(pos));
        }
    });
    m_formLayout->addRow("Position X:", m_xSpinBox);

    // Add Position Y
    m_ySpinBox = new QDoubleSpinBox();
    m_ySpinBox->setRange(-999999.0, 999999.0);
    m_ySpinBox->setDecimals(1);
    m_ySpinBox->setValue(pos.y());
    connect(m_ySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updatingProperties && m_currentNodeId != QtNodes::InvalidNodeId)
        {
            QPointF pos = m_graphModel->nodeData(m_currentNodeId, QtNodes::NodeRole::Position).toPointF();
            pos.setY(value);
            emit propertyChanged(m_currentNodeId, "position", QVariant::fromValue(pos));
        }
    });
    m_formLayout->addRow("Position Y:", m_ySpinBox);

    // TODO: Extract properties from embedded widget
    // This is complex and requires accessing the scene to get node delegate
    // For now, just show basic properties

    m_updatingProperties = false;
}

void PropertyEditor::onPropertyValueChanged()
{
    // This slot is called when any property widget value changes
    // The actual handling is done in the individual lambdas above
}

// ============================================================================
// QueueManager Implementation
// ============================================================================

QueueManager::QueueManager(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

QueueManager::~QueueManager()
{
    // Child widgets are automatically deleted by Qt
}

void QueueManager::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(10);

    // Placeholder content
    QLabel *titleLabel = new QLabel("Queue Manager");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    QLabel *infoLabel = new QLabel("Task queue management will be implemented in a future update.\n\n"
                                   "Features will include:\n"
                                   "* Queue and manage processing tasks\n"
                                   "* View task execution status\n"
                                   "* Pause and resume tasks\n"
                                   "* View execution history");
    infoLabel->setStyleSheet("color: #666666;");
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    mainLayout->addStretch();
}

// ============================================================================
// RightPanel Implementation
// ============================================================================

RightPanel::RightPanel(QWidget *parent)
    : QWidget(parent)
    , m_tabWidget(nullptr)
    , m_propertyEditor(nullptr)
    , m_queueManager(nullptr)
    , m_graphModel(nullptr)
    , m_selectedNodeId(QtNodes::InvalidNodeId)
{
    setupUi();
}

RightPanel::~RightPanel()
{
    // Child widgets are automatically deleted by Qt
}

void RightPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::North);
    m_tabWidget->setDocumentMode(true);

    // Add tabs
    setupPropertiesTab();
    setupQueueTab();

    mainLayout->addWidget(m_tabWidget);

    // Connect tab change signal
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &RightPanel::onTabChanged);

    // Set size
    setMinimumWidth(200);
    setMaximumWidth(350);
}

void RightPanel::setupPropertiesTab()
{
    m_propertyEditor = new PropertyEditor();
    connect(m_propertyEditor, &PropertyEditor::propertyChanged,
            this, &RightPanel::propertyChanged);
    m_tabWidget->addTab(m_propertyEditor, "Properties");
}

void RightPanel::setupQueueTab()
{
    m_queueManager = new QueueManager();
    m_tabWidget->addTab(m_queueManager, "Queue");
}

void RightPanel::setGraphModel(QtNodes::DataFlowGraphModel *model)
{
    m_graphModel = model;
    m_propertyEditor->setGraphModel(model);
}

void RightPanel::setSelectedNode(QtNodes::NodeId nodeId)
{
    m_selectedNodeId = nodeId;
    m_propertyEditor->setSelectedNode(nodeId);
}

void RightPanel::clearSelection()
{
    m_selectedNodeId = QtNodes::InvalidNodeId;
    m_propertyEditor->clearSelection();
}

void RightPanel::onTabChanged(int index)
{
    Q_UNUSED(index);
    // Handle tab-specific updates if needed
}
