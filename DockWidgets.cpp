#include "include/DockWidgets.h"
#include "include/NodeTreeWidget.h"
#include "NodeModels.h"

#include <QTreeWidgetItemIterator>
#include <QStyle>
#include <QFileInfo>
#include <algorithm>

// ============================================================================
// WorkflowBrowser Implementation
// ============================================================================

WorkflowBrowser::WorkflowBrowser(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Search box
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search workflows...");
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox);

    // Workflow list
    m_workflowList = new QTreeWidget();
    m_workflowList->setHeaderHidden(true);
    m_workflowList->setIndentation(12);
    layout->addWidget(m_workflowList);

    connect(m_searchBox, &QLineEdit::textChanged, this, &WorkflowBrowser::onSearchTextChanged);
    connect(m_workflowList, &QTreeWidget::itemDoubleClicked, this, &WorkflowBrowser::onItemDoubleClicked);
}

void WorkflowBrowser::refresh()
{
    m_workflowList->clear();
    if (m_workflowPath.isEmpty())
        return;

    QDir dir(m_workflowPath);
    if (!dir.exists())
        return;

    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::Readable);

    QFileInfoList files = dir.entryInfoList();
    for (const QFileInfo &fileInfo : files)
    {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_workflowList);
        item->setText(0, fileInfo.baseName());
        item->setData(0, Qt::UserRole, fileInfo.absoluteFilePath());
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    }

    if (m_workflowList->topLevelItemCount() == 0)
    {
        QTreeWidgetItem *emptyItem = new QTreeWidgetItem(m_workflowList);
        emptyItem->setText(0, "No workflows found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
    }
}

void WorkflowBrowser::onSearchTextChanged(const QString &text)
{
    QTreeWidgetItemIterator it(m_workflowList);
    while (*it)
    {
        QTreeWidgetItem *item = *it;
        QString itemName = item->text(0);

        bool match = text.isEmpty() ||
                   itemName.contains(text, Qt::CaseInsensitive);

        item->setHidden(!match);
        ++it;
    }

    // Show "No workflows found" if no matches
    bool hasVisible = false;
    for (int i = 0; i < m_workflowList->topLevelItemCount(); ++i)
    {
        if (!m_workflowList->topLevelItem(i)->isHidden())
        {
            hasVisible = true;
            break;
        }
    }

    if (m_workflowList->topLevelItemCount() > 0)
    {
        QTreeWidgetItem *emptyItem = m_workflowList->topLevelItem(0);
        if (emptyItem)
        emptyItem->setHidden(hasVisible);
    }
}

void WorkflowBrowser::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    if (!item)
        return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty())
        emit loadWorkflow(filePath);
}

// ============================================================================
// NodeLibraryWidget Implementation
// ============================================================================

NodeLibraryWidget::NodeLibraryWidget(QWidget *parent) : QWidget(parent)
    , m_registry(nullptr)
{
    setupUi();
}

void NodeLibraryWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Search box
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search nodes...");
    m_searchBox->setClearButtonEnabled(true);
    mainLayout->addWidget(m_searchBox);

    // Node tree widget
    m_nodeTree = new NodeTreeWidget();
    m_nodeTree->setHeaderHidden(true);
    m_nodeTree->setIndentation(12);
    mainLayout->addWidget(m_nodeTree);

    // Connect signals
    connect(m_searchBox, &QLineEdit::textChanged,
            this, &NodeLibraryWidget::onNodeSearchTextChanged);
    connect(m_nodeTree, &NodeTreeWidget::leafItemDoubleClicked,
            this, &NodeLibraryWidget::onNodeItemDoubleClicked);
    connect(m_nodeTree, &QTreeWidget::itemClicked,
            this, &NodeLibraryWidget::onNodeItemClicked);
}

void NodeLibraryWidget::setRegistry(std::shared_ptr<QtNodes::NodeDelegateModelRegistry> registry)
{
    m_registry = registry;
    populateNodeTree();
}

void NodeLibraryWidget::populateNodeTree()
{
    if (!m_registry)
        return;

    m_nodeTree->clear();

    auto models = m_registry->registeredModelsCategoryAssociation();

    // Group models by their paths
    QMap<QString, QList<QPair<QString, QString>>> pathModels;

    for (const auto &pair : models)
    {
        const QString &modelName = pair.first;
        const QString &categoryPath = pair.second;

        QStringList parts = categoryPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
        {
            // Top-level model without category - just use model name
            pathModels[modelName].append(QPair<QString, QString>(modelName, modelName));
        }
        else
        {
            pathModels[categoryPath].append(QPair<QString, QString>(modelName, modelName));
        }
    }

    // Build tree structure respecting the order
    QMap<QString, QTreeWidgetItem*> topItems;

    for (auto it = pathModels.begin(); it != pathModels.end(); ++it)
    {
        const QString &categoryPath = it.key();
        const QList<QPair<QString, QString>> &modelsList = it.value();

        QStringList parts = categoryPath.split('/', Qt::SkipEmptyParts);
        QTreeWidgetItem *currentParent = nullptr;

        // Build/create category hierarchy
        for (int i = 0; i < parts.size(); ++i)
        {
            QString categoryPart = parts[i];
            QString categoryPathSoFar;
            for (int j = 0; j <= i; ++j)
            {
                if (j > 0)
                    categoryPathSoFar += '/';
                categoryPathSoFar += parts[j];
            }

            // Find or create this category item
            if (!topItems.contains(categoryPathSoFar))
            {
                bool isLeaf = (i == parts.size() - 1);
                QTreeWidgetItem *newItem = new QTreeWidgetItem(m_nodeTree);
                newItem->setText(0, isLeaf ? modelsList.first().second : categoryPart);
                newItem->setFlags(isLeaf ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                        : Qt::ItemIsEnabled);
                topItems[categoryPathSoFar] = newItem;

                if (i == 0)
                {
                    // Add as top-level item
                }
                else
                {
                    QString parentPath;
                    for (int j = 0; j < i; ++j)
                    {
                        if (j > 0)
                            parentPath += '/';
                        parentPath += parts[j];
                    }
                    if (topItems.contains(parentPath))
                    {
                        topItems[parentPath]->addChild(newItem);
                        break;
                    }
                }
            }

            currentParent = topItems[categoryPathSoFar];
        }

        // Add models if this is a leaf category
        if (currentParent)
        {
            // Sort models alphabetically by display name
            QList<QPair<QString, QString>> sortedModels = modelsList;
            std::sort(sortedModels.begin(), sortedModels.end(),
                [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                    return a.second < b.second;
                });

            for (const auto &modelInfo : sortedModels)
            {
                QTreeWidgetItem *item = new QTreeWidgetItem(currentParent);
                item->setText(0, modelInfo.second);
                item->setData(0, Qt::UserRole, modelInfo.first);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
            currentParent->setExpanded(true);
        }
    }

    // Expand top-level items
    for (int i = 0; i < m_nodeTree->topLevelItemCount(); ++i)
    {
        m_nodeTree->topLevelItem(i)->setExpanded(true);
    }
}

void NodeLibraryWidget::onNodeSearchTextChanged(const QString &text)
{
    QTreeWidgetItemIterator it(m_nodeTree);
    while (*it)
    {
        QTreeWidgetItem *item = *it;
        QString itemName = item->text(0);

        bool match = text.isEmpty() ||
                   itemName.contains(text, Qt::CaseInsensitive);

        // Leaf nodes (items without children) - these are clickable nodes
        if (item->childCount() == 0)
        {
            // Show/hide based on match
            item->setHidden(!match);
            // If matching, make sure all parent categories are visible and expanded
            if (match)
            {
                QTreeWidgetItem *parent = item->parent();
                while (parent)
                {
                    parent->setHidden(false);
                    parent->setExpanded(true);
                    parent = parent->parent();
                }
            }
        }
        ++it;
    }
}

void NodeLibraryWidget::onNodeItemDoubleClicked(const QString &modelName)
{
    if (!modelName.isEmpty())
        emit nodeDoubleClicked(modelName);
}

void NodeLibraryWidget::onNodeItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    if (!item)
        return;

    QString modelName = item->data(0, Qt::UserRole).toString();
    emit nodeItemClicked(modelName);
}

// ============================================================================
// PropertyEditor Implementation
// ============================================================================

PropertyEditor::PropertyEditor(QWidget *parent)
    : QWidget(parent)
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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create scroll area
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Create content widget
    m_contentWidget = new QWidget();
    m_contentWidget->setMinimumWidth(200);

    auto *contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(12);

    // Form layout for properties
    m_formLayout = new QFormLayout();
    m_formLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addLayout(m_formLayout);
    contentLayout->addStretch();

    m_scrollArea->setWidget(m_contentWidget);
    layout->addWidget(m_scrollArea);

    // Initial state
    m_noSelectionLabel = new QLabel("No node selected");
    m_noSelectionLabel->setAlignment(Qt::AlignCenter);
    m_noSelectionLabel->setStyleSheet("color: #888888; font padding: 8px;");
    m_contentWidget->layout()->addWidget(m_noSelectionLabel);
}

void PropertyEditor::setGraphModel(QtNodes::DataFlowGraphModel *model)
{
    m_graphModel = model;
}

void PropertyEditor::setSelectedNode(QtNodes::NodeId nodeId)
{
    if (m_currentNodeId == nodeId)
        return;

    m_currentNodeId = nodeId;
    clearProperties();

    if (!m_graphModel || nodeId == QtNodes::InvalidNodeId)
    {
        m_nodeIdLabel = nullptr;
        m_noSelectionLabel->show();
        m_contentWidget->layout()->removeWidget(m_nodeIdLabel);
        return;
    }

    m_noSelectionLabel->hide();

    // Add node ID display
    m_nodeIdLabel = new QLabel("Node ID: " + QString::number(static_cast<int>(nodeId)));
    m_nodeIdLabel->setStyleSheet("color: #AAAAAA; font-weight: bold; font-size: 11px; padding: 4px;");
    static_cast<QVBoxLayout*>(m_contentWidget->layout())->insertWidget(0, m_nodeIdLabel);

    generateProperties(nodeId);
}

void PropertyEditor::clearSelection()
{
    m_currentNodeId = QtNodes::InvalidNodeId;
    clearProperties();

    m_nodeIdLabel = nullptr;
    m_noSelectionLabel->show();
    m_contentWidget->layout()->removeWidget(m_nodeIdLabel);
}

void PropertyEditor::generateProperties(QtNodes::NodeId nodeId)
{
    // Clear existing properties
    while (QLayoutItem *item = m_formLayout->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    // Get node data
    QString caption = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Caption).toString();
    m_captionEdit = new QLineEdit(caption);
    m_captionEdit->setPlaceholderText("Enter node caption...");
    connect(m_captionEdit, &QLineEdit::textChanged, this, &PropertyEditor::onPropertyValueChanged);
    m_formLayout->addRow("Caption:", m_captionEdit);

    // Position property
    QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).value<QPointF>();

    m_xSpinBox = new QDoubleSpinBox();
    m_xSpinBox->setRange(-1e6, 1e6);
    m_xSpinBox->setDecimals(2);
    m_xSpinBox->setValue(pos.x());
    connect(m_xSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertyEditor::onPropertyValueChanged);
    m_formLayout->addRow("X:", m_xSpinBox);

    m_ySpinBox = new QDoubleSpinBox();
    m_ySpinBox->setRange(-1e6, 1e6);
    m_ySpinBox->setDecimals(2);
    m_ySpinBox->setValue(pos.y());
    connect(m_ySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertyEditor::onPropertyValueChanged);
    m_formLayout->addRow("Y:", m_ySpinBox);

    // Check for embedded widget and extract properties
    auto delegateModel = m_graphModel->delegateModel<QtNodes::NodeDelegateModel>(nodeId);
    if (delegateModel)
    {
        QWidget *embeddedWidget = delegateModel->embeddedWidget();
        if (embeddedWidget)
        {
            extractPropertiesFromWidget(embeddedWidget, m_formLayout, nodeId);
        }
    }
}

void PropertyEditor::clearProperties()
{
    while (QLayoutItem *item = m_formLayout->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    m_captionEdit = nullptr;
    m_xSpinBox = nullptr;
    m_ySpinBox = nullptr;
}

void PropertyEditor::extractPropertiesFromWidget(QWidget *widget, QFormLayout *layout, QtNodes::NodeId nodeId)
{
    // Find all input widgets (QLineEdit, QSpinBox, QDoubleSpinBox, QCheckBox, QComboBox)
    QList<QWidget*> children = widget->findChildren<QWidget*>();

    for (QWidget *child : children)
    {
        QLineEdit *lineEdit = qobject_cast<QLineEdit*>(child);
        QSpinBox *spinBox = qobject_cast<QSpinBox*>(child);
        QDoubleSpinBox *doubleSpinBox = qobject_cast<QDoubleSpinBox*>(child);
        QCheckBox *checkBox = qobject_cast<QCheckBox*>(child);
        QComboBox *comboBox = qobject_cast<QComboBox*>(child);

        QString label = "";
        QString propertyName = "";

        // Get property name from object name
        QString objectName = child->objectName();

        if (lineEdit)
        {
            label = lineEdit->text();
            propertyName = objectName.isEmpty() ? "Value" : objectName;
            QLineEdit *propEdit = new QLineEdit(label);
            propEdit->setPlaceholderText("Enter value...");
            connect(propEdit, &QLineEdit::textChanged, this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propertyName, propEdit);

            // Store original widget for updates
            m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, QVariant::fromValue(propEdit));
        }
        else if (spinBox)
        {
            int value = spinBox->value();
            propertyName = objectName.isEmpty() ? "Value" : objectName;
            QSpinBox *propSpinBox = new QSpinBox();
            propSpinBox->setRange(spinBox->minimum(), spinBox->maximum());
            propSpinBox->setSingleStep(spinBox->singleStep());
            propSpinBox->setValue(value);
            connect(propSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propertyName, propSpinBox);
        }
        else if (doubleSpinBox)
        {
            double value = doubleSpinBox->value();
            propertyName = objectName.isEmpty() ? "Value" : objectName;
            QDoubleSpinBox *propDoubleSpinBox = new QDoubleSpinBox();
            propDoubleSpinBox->setRange(doubleSpinBox->minimum(), doubleSpinBox->maximum());
            propDoubleSpinBox->setDecimals(doubleSpinBox->decimals());
            propDoubleSpinBox->setSingleStep(doubleSpinBox->singleStep());
            propDoubleSpinBox->setValue(value);
            connect(propDoubleSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propertyName, propDoubleSpinBox);
        }
        else if (checkBox)
        {
            bool value = checkBox->isChecked();
            propertyName = objectName.isEmpty() ? "Value" : objectName;
            QCheckBox *propCheckBox = new QCheckBox(propertyName);
            propCheckBox->setChecked(value);
            connect(propCheckBox, &QCheckBox::stateChanged, this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propCheckBox);
        }
        else if (comboBox)
        {
            int index = comboBox->currentIndex();
            propertyName = objectName.isEmpty() ? "Value" : objectName;
            QComboBox *propComboBox = new QComboBox();
            // Copy all items from source combo box
            for (int i = 0; i < comboBox->count(); ++i) {
                propComboBox->addItem(comboBox->itemText(i));
            }
            propComboBox->setCurrentIndex(index);
            connect(propComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propertyName, propComboBox);
        }
    }
}

void PropertyEditor::onPropertyValueChanged()
{
    if (m_updatingProperties || !m_graphModel || m_currentNodeId == QtNodes::InvalidNodeId)
        return;

    m_updatingProperties = true;

    // Update caption
    if (m_captionEdit)
    {
        QString caption = m_captionEdit->text();
        m_graphModel->setNodeData(m_currentNodeId, QtNodes::NodeRole::Caption, caption);
    }

    // Update position
    if (m_xSpinBox && m_ySpinBox)
    {
        QPointF pos(m_xSpinBox->value(), m_ySpinBox->value());
        m_graphModel->setNodeData(m_currentNodeId, QtNodes::NodeRole::Position, pos);
    }

    m_updatingProperties = false;
}

// ============================================================================
// QueueManagerWidget Implementation
// ============================================================================

QueueManagerWidget::QueueManagerWidget(QWidget *parent) : QWidget(parent)
{
    setupUi();
}

void QueueManagerWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    QLabel *label = new QLabel("Execution Queue");
    label->setStyleSheet("font-size: 14px; font-weight: bold; color: #CCCCCC; padding: 8px;");
    layout->addWidget(label);

    QLabel *placeholder = new QLabel("Queue functionality coming soon...");
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("color: #666666; padding: 20px;");
    layout->addWidget(placeholder);

    layout->addStretch();
}
