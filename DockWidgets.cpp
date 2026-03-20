#include "include/DockWidgets.h"
#include "include/NodeTreeWidget.h"
#include "include/PaletteOrder.h"
#include "NodeModels.h"

#include <QTreeWidgetItemIterator>
#include <QStyle>
#include <QFileInfo>
#include <QSet>
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

void NodeLibraryWidget::setPaletteOrder(const PaletteOrder& order)
{
    m_paletteOrder.topLevel = order.topLevel;
    m_paletteOrder.subcategories = order.subcategories;
    m_paletteOrder.leafItems = order.leafItems;
    populateNodeTree();
}

void NodeLibraryWidget::populateNodeTree()
{
    if (!m_registry)
        return;

    m_nodeTree->clear();

    auto models = m_registry->registeredModelsCategoryAssociation();

    // Build a map: category path -> list of (modelName, caption)
    QMap<QString, QList<QPair<QString, QString>>> pathModels;

    for (const auto &pair : models)
    {
        const QString &modelName = pair.first;
        const QString &categoryPath = pair.second;

        // Create model instance to get caption
        auto model = m_registry->create(modelName);
        if (!model)
            continue;

        QString caption = model->caption();

        QStringList parts = categoryPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        if (parts.size() == 1)
        {
            // 1-level: path is category (e.g., "Test")
            pathModels[categoryPath].append(qMakePair(modelName, caption));
        }
        else if (parts.size() == 2)
        {
            // 2-level: e.g., "Test/SimpleSource"
            pathModels[categoryPath].append(qMakePair(modelName, caption));
        }
        else
        {
            // 3-level: e.g., "Data Import/Sentinel-1/Single Import"
            QString topLevel = parts[0];
            QString subcategory = parts[1];
            QString leafPath = topLevel + "/" + subcategory;
            pathModels[leafPath].append(qMakePair(modelName, caption));
        }
    }

    // Process top-level categories in palette order
    for (const QString &topLevel : m_paletteOrder.topLevel)
    {
        if (topLevel.isEmpty())
            continue;

        // Check if this top-level has any models registered
        bool hasModels = false;
        for (auto it = pathModels.constBegin(); it != pathModels.constEnd(); ++it)
        {
            const QString &path = it.key();
            if (path == topLevel || path.startsWith(topLevel + "/"))
            {
                hasModels = true;
                break;
            }
        }

        if (!hasModels)
            continue;

        // Create top-level item
        QTreeWidgetItem *topItem = new QTreeWidgetItem(m_nodeTree);
        topItem->setText(0, topLevel);
        topItem->setExpanded(false);
        topItem->setFlags(Qt::ItemIsEnabled);

        // Process subcategories for this top-level in palette order
        QStringList subcategories = m_paletteOrder.subcategories.value(topLevel);
        for (const QString &subcategory : subcategories)
        {
            if (subcategory.isEmpty())
                continue;

            QString subPath = topLevel + "/" + subcategory;

            // Check if this subcategory has models
            if (!pathModels.contains(subPath))
                continue;

            // Create subcategory item
            QTreeWidgetItem *subItem = new QTreeWidgetItem(topItem);
            subItem->setText(0, subcategory);
            subItem->setExpanded(false);
            subItem->setFlags(Qt::ItemIsEnabled);

            // Add leaf items in palette order
            QList<PaletteOrder::LeafItem> leafOrder = m_paletteOrder.leafItems.value(subPath);
            const QList<QPair<QString, QString>> &modelsList = pathModels[subPath];

            // Build a map: caption -> modelName for matching
            QMap<QString, QString> captionToModelName;
            for (const auto &modelPair : modelsList)
            {
                captionToModelName[modelPair.second] = modelPair.first;
            }

            // Track models that have been added
            QSet<QString> addedModelNames;

            // Match leafOrder items by caption to find correct modelName
            for (const PaletteOrder::LeafItem &leafItemInfo : leafOrder)
            {
                const QString &displayName = leafItemInfo.displayName;
                const QString &caption = leafItemInfo.caption;

                if (caption.isEmpty())
                    continue;

                // Find the model by caption
                if (captionToModelName.contains(caption))
                {
                    const QString &modelName = captionToModelName[caption];
                    QTreeWidgetItem *leafItem = new QTreeWidgetItem(subItem);
                    leafItem->setText(0, displayName);  // Use display name for UI
                    leafItem->setData(0, Qt::UserRole, modelName);
                    leafItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    addedModelNames.insert(modelName);
                }
            }

            // Add any remaining models not in leafOrder
            for (const auto &modelPair : modelsList)
            {
                const QString &modelName = modelPair.first;
                const QString &caption = modelPair.second;
                if (!addedModelNames.contains(modelName))
                {
                    QTreeWidgetItem *leafItem = new QTreeWidgetItem(subItem);
                    leafItem->setText(0, caption);
                    leafItem->setData(0, Qt::UserRole, modelName);
                    leafItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    addedModelNames.insert(modelName);
                }
            }

            subItem->setExpanded(true);
        }

        // Handle direct models under top-level (for "Test" and "Note" cases)
        if (pathModels.contains(topLevel))
        {
            const QList<QPair<QString, QString>> &modelsList = pathModels[topLevel];

            // Build a map: caption -> modelName for matching
            QMap<QString, QString> captionToModelName;
            for (const auto &modelPair : modelsList)
            {
                captionToModelName[modelPair.second] = modelPair.first;
            }

            // Track models that have been added
            QSet<QString> addedModelNames;

            // Match leafOrder items by caption to find correct modelName
            QList<PaletteOrder::LeafItem> leafOrder = m_paletteOrder.leafItems.value(topLevel);
            for (const PaletteOrder::LeafItem &leafItemInfo : leafOrder)
            {
                const QString &displayName = leafItemInfo.displayName;
                const QString &caption = leafItemInfo.caption;

                if (caption.isEmpty())
                    continue;

                // Find the model by caption
                if (captionToModelName.contains(caption))
                {
                    const QString &modelName = captionToModelName[caption];
                    QTreeWidgetItem *leafItem = new QTreeWidgetItem(topItem);
                    leafItem->setText(0, displayName);  // Use display name for UI
                    leafItem->setData(0, Qt::UserRole, modelName);
                    leafItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    addedModelNames.insert(modelName);
                }
            }

            // Add any remaining models not in leafOrder
            for (const auto &modelPair : modelsList)
            {
                const QString &modelName = modelPair.first;
                const QString &caption = modelPair.second;
                if (!addedModelNames.contains(modelName))
                {
                    QTreeWidgetItem *leafItem = new QTreeWidgetItem(topItem);
                    leafItem->setText(0, caption);
                    leafItem->setData(0, Qt::UserRole, modelName);
                    leafItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    addedModelNames.insert(modelName);
                }
            }
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
    , m_nodeIdLabel(nullptr)
    , m_captionEdit(nullptr)
    , m_xSpinBox(nullptr)
    , m_ySpinBox(nullptr)
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
    m_noSelectionLabel->setStyleSheet("color: #888888; font-style: italic; padding: 8px;");
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

    // 清理旧的 m_nodeIdLabel（如果存在）
    if (m_nodeIdLabel)
    {
        m_contentWidget->layout()->removeWidget(m_nodeIdLabel);
        m_nodeIdLabel->deleteLater();
        m_nodeIdLabel = nullptr;
    }

    clearProperties();

    m_noSelectionLabel->hide();

    if (!m_graphModel || nodeId == QtNodes::InvalidNodeId)
    {
        m_noSelectionLabel->show();
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

    // 先移除旧的 m_nodeIdLabel（如果存在）
    if (m_nodeIdLabel)
    {
        m_contentWidget->layout()->removeWidget(m_nodeIdLabel);
        m_nodeIdLabel->deleteLater();
        m_nodeIdLabel = nullptr;  // 立即置空，防止访问已删除对象
    }

    clearProperties();

    m_noSelectionLabel->show();
}

void PropertyEditor::generateProperties(QtNodes::NodeId nodeId)
{
    // Clear existing properties and m_nodeIdLabel
    while (QLayoutItem *item = m_formLayout->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    // 注意：不在这里删除 m_nodeIdLabel，因为它由 setSelectedNode 管理

    // Get node data
    QString caption = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Caption).toString();
    m_captionEdit = new QLineEdit(caption);
    m_captionEdit->setPlaceholderText("Enter node caption...");
    m_captionEdit->setReadOnly(true);  // Caption is read-only
    m_captionEdit->setStyleSheet("background-color: #F0F0F0;");
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

    // Add separator line between basic properties and widget properties
    QFrame *separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setStyleSheet("background-color: #CCCCCC;");
    m_formLayout->addRow(separator);

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

QString PropertyEditor::getLabelForWidget(QWidget *widget)
{
    if (!widget)
        return "";

    // First check if there's a buddy label set
    QList<QLabel*> labels = widget->findChildren<QLabel*>();
    for (QLabel *label : labels)
    {
        if (label->buddy() == widget && !label->text().isEmpty())
            return label->text();
    }

    // Try to find label in the widget's layout (sibling labels)
    QLayout *currentLayout = nullptr;
    if (widget->parentWidget())
        currentLayout = widget->parentWidget()->layout();
    while (currentLayout)
    {
        // Check all layout items
        for (int i = 0; i < currentLayout->count(); ++i)
        {
            QLayoutItem *item = currentLayout->itemAt(i);
            if (!item)
                continue;

            // Check if this is a label that could be for this widget
            QLabel *label = qobject_cast<QLabel*>(item->widget());
            if (label && !label->text().isEmpty())
            {
                // If this label has this widget as buddy, use it
                if (label->buddy() == widget)
                    return label->text();
            }

            // Check nested layouts (like QHBoxLayout)
            QLayout *childLayout = item->layout();
            if (childLayout)
            {
                for (int j = 0; j < childLayout->count(); ++j)
                {
                    QLayoutItem *childItem = childLayout->itemAt(j);
                    if (!childItem)
                        continue;

                    QLabel *childLabel = qobject_cast<QLabel*>(childItem->widget());
                    if (childLabel && !childLabel->text().isEmpty())
                    {
                        // Check if this is a label for the widget (based on buddy or position)
                        if (childLabel->buddy() == widget)
                            return childLabel->text();
                    }

                    // Check if this item is our widget, then look for labels before it
                    if (childItem->widget() == widget && j > 0)
                    {
                        // Look for a label in the same layout before this widget
                        for (int k = j - 1; k >= 0; --k)
                        {
                            QLayoutItem *siblingItem = childLayout->itemAt(k);
                            if (!siblingItem)
                                continue;
                            QLabel *siblingLabel = qobject_cast<QLabel*>(siblingItem->widget());
                            if (siblingLabel && !siblingLabel->text().isEmpty())
                            {
                                // Check object name pattern (label_XXX)
                                QString labelName = siblingLabel->objectName().toLower();
                                if (labelName.startsWith("label") && !labelName.contains("file"))
                                    return siblingLabel->text();
                                return siblingLabel->text();
                            }
                            // If we hit another input widget, stop looking
                            QWidget *siblingWidget = siblingItem->widget();
                            if (qobject_cast<QLineEdit*>(siblingWidget) ||
                                qobject_cast<QSpinBox*>(siblingWidget) ||
                                qobject_cast<QDoubleSpinBox*>(siblingWidget) ||
                                qobject_cast<QCheckBox*>(siblingWidget) ||
                                qobject_cast<QComboBox*>(siblingWidget))
                            {
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Move to parent layout
        QWidget *layoutParentWidget = currentLayout->parentWidget();
        if (layoutParentWidget && layoutParentWidget->parentWidget())
            currentLayout = layoutParentWidget->parentWidget()->layout();
        else
            currentLayout = nullptr;
    }

    return "";
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

        // Get property name from widget label or object name
        QString labelFromLayout = getLabelForWidget(child);
        QString objectName = child->objectName();
        propertyName = labelFromLayout.isEmpty() ? (objectName.isEmpty() ? "Value" : objectName) : labelFromLayout;

        if (lineEdit)
        {
            label = lineEdit->text();
            QLineEdit *propEdit = new QLineEdit(label);
            propEdit->setPlaceholderText("Enter value...");
            connect(propEdit, &QLineEdit::textChanged, this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propertyName, propEdit);

            // Store original widget for updates
        }
        else if (spinBox)
        {
            int value = spinBox->value();
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
            QCheckBox *propCheckBox = new QCheckBox(propertyName);
            propCheckBox->setChecked(value);
            connect(propCheckBox, &QCheckBox::stateChanged, this, &PropertyEditor::onPropertyValueChanged);
            layout->addRow(propCheckBox);
        }
        else if (comboBox)
        {
            int index = comboBox->currentIndex();
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
