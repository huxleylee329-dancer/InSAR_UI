#include "include/DockWidgets.h"
#include "include/NodeTreeWidget.h"
#include "include/PaletteOrder.h"
#include "NodeModels.h"

#include <QTreeWidgetItemIterator>
#include <QStyle>
#include <QFileInfo>
#include <QSet>
#include <QFileDialog>
#include <QToolButton>

// QtNodes headers
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/internal/NodeDataSnapshot.hpp>
#include <QtNodes/internal/NodeDetailWindow.hpp>

// ============================================================================
// WorkflowBrowser Implementation
// ============================================================================

WorkflowBrowser::WorkflowBrowser(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Ensure this widget expands to fill available space
    QSizePolicy sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Expanding);
    setSizePolicy(sp);

    // Search box
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search workflows...");
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox);

    // Workflow list - ensure it expands with the container
    m_workflowList = new QTreeWidget();
    m_workflowList->setHeaderHidden(true);
    m_workflowList->setIndentation(12);
    QSizePolicy listSp = m_workflowList->sizePolicy();
    listSp.setHorizontalPolicy(QSizePolicy::Expanding);
    listSp.setVerticalPolicy(QSizePolicy::Expanding);
    m_workflowList->setSizePolicy(listSp);
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

    // Ensure this widget expands to fill available space
    QSizePolicy sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Expanding);
    setSizePolicy(sp);

    // Search box
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search nodes...");
    m_searchBox->setClearButtonEnabled(true);
    mainLayout->addWidget(m_searchBox);

    // Node tree widget - ensure it expands with the container
    m_nodeTree = new NodeTreeWidget();
    m_nodeTree->setHeaderHidden(true);
    m_nodeTree->setIndentation(12);
    QSizePolicy treeSp = m_nodeTree->sizePolicy();
    treeSp.setHorizontalPolicy(QSizePolicy::Expanding);
    treeSp.setVerticalPolicy(QSizePolicy::Expanding);
    m_nodeTree->setSizePolicy(treeSp);
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
    , m_isExecutable(false)
    , m_nodeIdLabel(nullptr)
    , m_xSpinBox(nullptr)
    , m_ySpinBox(nullptr)
    , m_executionStateLabel(nullptr)
    , m_progressBar(nullptr)
    , m_modeLabel(nullptr)
{
    m_nodeData.progress = 0;
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
    layout->setSpacing(0);

    // Ensure this widget expands to fill available space
    QSizePolicy sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Expanding);
    setSizePolicy(sp);

    // === 顶部固定区域（Node ID + 基本信息）===
    m_fixedTopWidget = new QWidget();
    m_fixedTopLayout = new QVBoxLayout(m_fixedTopWidget);
    m_fixedTopLayout->setContentsMargins(8, 8, 8, 8);
    m_fixedTopLayout->setSpacing(8);

    // "No selection" 标签
    m_noSelectionLabel = new QLabel("No node selected");
    m_noSelectionLabel->setAlignment(Qt::AlignCenter);
    bool darkTheme = isDarkTheme();
    QString noSelectionTextColor = darkTheme ? "#94A3B8" : "#94A3B8";
    m_noSelectionLabel->setStyleSheet(QString("color: %1;").arg(noSelectionTextColor));
    m_fixedTopLayout->addWidget(m_noSelectionLabel);

    layout->addWidget(m_fixedTopWidget);

    // === 底部可滚动区域（三个 CollapsibleSection）===
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    // Ensure scroll area expands with container
    QSizePolicy scrollSp = m_scrollArea->sizePolicy();
    scrollSp.setHorizontalPolicy(QSizePolicy::Expanding);
    scrollSp.setVerticalPolicy(QSizePolicy::Expanding);
    m_scrollArea->setSizePolicy(scrollSp);

    m_contentWidget = new QWidget();
    // Minimum width for content - should match overall PropertyEditor minimum width
    m_contentWidget->setMinimumWidth(200);

    m_mainLayout = new QVBoxLayout(m_contentWidget);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(8);
    // 移除 addStretch() - 它在所有内容之前添加，会导致内容被推下去

    // 创建三个可折叠区域（移除独立的滚动区域）
    createCollapsibleSection(m_inputSection, "Input Data");
    createCollapsibleSection(m_processingSection, "Processing Info");
    createCollapsibleSection(m_outputSection, "Output Data");

    m_mainLayout->addWidget(m_inputSection.container);
    m_mainLayout->addWidget(m_processingSection.container);
    m_mainLayout->addWidget(m_outputSection.container);
    // 添加一个最终的 stretch 来吸收所有额外空间
    // 这样三个折叠区只会占据实际内容大小，剩余空间留空
    m_mainLayout->addStretch();

    m_scrollArea->setWidget(m_contentWidget);
    // 设置内容靠上对齐，避免内容在 ScrollArea 中垂直居中
    m_scrollArea->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_scrollArea);

    // 初始隐藏可滚动区域
    m_scrollArea->hide();
}

void PropertyEditor::createCollapsibleSection(CollapsibleSection& section, const QString& title)
{
    bool darkTheme = isDarkTheme();

    section.container = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(section.container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Create header with glass effect
    section.header = new QWidget();
    QString headerBg = darkTheme ? "rgba(64, 64, 64, 0.6)" : "rgba(241, 245, 249, 0.5)";
    QString headerBorder = darkTheme ? "rgba(148, 163, 184, 0.2)" : "rgba(148, 163, 184, 0.15)";
    QString headerTextColor = darkTheme ? "#FFFFFF" : "#1E3A8A";

    section.header->setStyleSheet(QString(
        "QWidget {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "}"
    ).arg(headerBg).arg(headerBorder));

    QHBoxLayout* headerLayout = new QHBoxLayout(section.header);
    headerLayout->setContentsMargins(10, 10, 10, 10);
    headerLayout->setSpacing(8);

    section.toggleButton = new QToolButton();
    section.toggleButton->setArrowType(Qt::ArrowType::DownArrow);  // 默认展开
    section.toggleButton->setMaximumWidth(24);
    section.toggleButton->setMinimumWidth(24);
    QString toggleButtonColor = darkTheme ? "#94A3B8" : "#3B82F6";
    section.toggleButton->setStyleSheet(QString(
        "QToolButton {"
        "  border: none;"
        "  background: transparent;"
        "  color: %1;"
        "}"
        "QToolButton:hover {"
        "  color: %2;"
        "}"
    ).arg(toggleButtonColor).arg(darkTheme ? "#FFFFFF" : "#60A5FA"));
    connect(section.toggleButton, &QToolButton::clicked, this, [this, &section]() {
        toggleSection(section);
    });

    section.titleLabel = new QLabel(title);
    section.titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(headerTextColor));

    headerLayout->addWidget(section.toggleButton);
    headerLayout->addWidget(section.titleLabel);
    headerLayout->addStretch();

    layout->addWidget(section.header);

    // 直接使用 contentWidget 作为内容容器（移除独立的 scrollArea）
    section.contentWidget = new QWidget();
    section.contentWidget->setStyleSheet("QWidget { background: transparent; }");

    QVBoxLayout* contentLayout = new QVBoxLayout(section.contentWidget);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(6);
    // 不再添加 stretch，区域按实际内容大小排列

    layout->addWidget(section.contentWidget);

    section.isExpanded = true;  // 默认全部展开
}

void PropertyEditor::toggleSection(CollapsibleSection& section)
{
    section.isExpanded = !section.isExpanded;

    if (section.isExpanded) {
        section.toggleButton->setArrowType(Qt::ArrowType::DownArrow);
        section.contentWidget->show();
    } else {
        section.toggleButton->setArrowType(Qt::ArrowType::RightArrow);
        section.contentWidget->hide();
    }
}

bool PropertyEditor::isDarkTheme() const
{
    QVariant bgColor = property("theme-background");
    if (bgColor.isValid()) {
        QColor color = bgColor.value<QColor>();
        if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
            return true;
        }
    }
    return false;
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

    // 清理旧的 m_nodeIdLabel（从固定顶部区域）
    if (m_nodeIdLabel)
    {
        m_fixedTopLayout->removeWidget(m_nodeIdLabel);
        m_nodeIdLabel->deleteLater();
        m_nodeIdLabel = nullptr;
    }

    // 清理 basicInfoWidget（包含 captionEdit 等控件）
    // 注意：这些控件是在 basicInfoWidget 内部，不要单独删除
    clearBasicInfoFromLayout();

    // 重置指向 basicInfoWidget 内部控件的指针
    // 因为删除 basicInfoWidget 时，这些控件也被删除了
    m_xSpinBox = nullptr;
    m_ySpinBox = nullptr;
    m_executionStateLabel = nullptr;
    m_progressBar = nullptr;
    m_modeLabel = nullptr;

    clearProperties();

    m_noSelectionLabel->hide();

    if (!m_graphModel || nodeId == QtNodes::InvalidNodeId)
    {
        m_noSelectionLabel->show();
        // 隐藏可滚动区域
        m_scrollArea->hide();
        return;
    }

    // Capture node data
    captureNodeData(nodeId);

    m_noSelectionLabel->hide();
    m_scrollArea->show();  // 显示可滚动区域

    // Clear old separator and basicInfoWidget from layout
    clearBasicInfoFromLayout();

    generateProperties();
}

void PropertyEditor::clearSelection()
{
    m_currentNodeId = QtNodes::InvalidNodeId;

    // 先移除旧的 m_nodeIdLabel（如果存在）
    if (m_nodeIdLabel)
    {
        m_fixedTopLayout->removeWidget(m_nodeIdLabel);
        m_nodeIdLabel->deleteLater();
        m_nodeIdLabel = nullptr;  // 立即置空，防止访问已删除对象
    }

    clearProperties();

    // Clear any remaining basic info from layout
    clearBasicInfoFromLayout();

    // Reset pointers to controls inside basicInfoWidget
    // (they are deleted when basicInfoWidget is deleted)
    m_xSpinBox = nullptr;
    m_ySpinBox = nullptr;
    m_executionStateLabel = nullptr;
    m_progressBar = nullptr;
    m_modeLabel = nullptr;

    // Hide scroll area
    m_scrollArea->hide();

    m_noSelectionLabel->show();
}

void PropertyEditor::refreshCurrentNode()
{
    // Only refresh if there's a currently selected node
    if (m_currentNodeId == QtNodes::InvalidNodeId || !m_graphModel) {
        return;
    }

    // Re-capture node data (this updates m_nodeData with latest values)
    captureNodeData(m_currentNodeId);

    // Clear old node ID label
    if (m_nodeIdLabel) {
        m_fixedTopLayout->removeWidget(m_nodeIdLabel);
        m_nodeIdLabel->deleteLater();
        m_nodeIdLabel = nullptr;
    }

    // Clear old basic info widget (caption, position, etc.)
    clearBasicInfoFromLayout();

    // Clear collapsible section content
    clearProperties();

    // Regenerate all sections with updated data
    generateProperties();
}

void PropertyEditor::generateProperties()
{
    // Note: clearProperties() is already called in setSelectedNode() before this method
    // No need to clear again here since m_nodeData was updated by captureNodeData()

    bool darkTheme = isDarkTheme();

    // Add node ID display to fixed top area
    m_nodeIdLabel = new QLabel("Node ID: " + QString::number(static_cast<int>(m_currentNodeId)));
    QString nodeIdTextColor = darkTheme ? "#94A3B8" : "#64748B";
    m_nodeIdLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(nodeIdTextColor));
    m_fixedTopLayout->insertWidget(0, m_nodeIdLabel);

    // Generate each section
    generateBasicInfoSection();
    generateInputSection();
    generateProcessingSection();
    generateOutputSection();
}

void PropertyEditor::generateBasicInfoSection()
{
    bool darkTheme = isDarkTheme();

    // Create basic info container with glass effect
    QWidget* basicInfoWidget = new QWidget();
    basicInfoWidget->setStyleSheet(QString(
        "QWidget {"
        "  background-color: %1;"
        "  border-radius: 8px;"
        "}"
    ).arg(darkTheme ? "rgba(64, 64, 64, 0.5)" : "rgba(255, 255, 255, 0.7)"));

    QVBoxLayout* basicInfoLayout = new QVBoxLayout(basicInfoWidget);
    basicInfoLayout->setContentsMargins(12, 12, 12, 12);
    basicInfoLayout->setSpacing(8);

    QString primaryTextColor = darkTheme ? "#FFFFFF" : "#1E3A8A";
    QString secondaryTextColor = darkTheme ? "#94A3B8" : "#334155";
    QString inputBgColor = darkTheme ? "rgba(64, 64, 64, 0.8)" : "rgba(255, 255, 255, 0.9)";
    QString inputBorderColor = darkTheme ? "rgba(148, 163, 184, 0.3)" : "#CBD5E1";

    // Caption - 使用横向布局在一行显示
    QHBoxLayout* captionLayout = new QHBoxLayout();
    captionLayout->setSpacing(4);

    QLabel* captionLabel = new QLabel("Caption:");
    captionLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(primaryTextColor));

    QLabel* captionValueLabel = new QLabel(m_nodeData.caption);
    captionValueLabel->setStyleSheet(QString("color: %1;").arg(primaryTextColor));
    captionValueLabel->setWordWrap(true);

    captionLayout->addWidget(captionLabel);
    captionLayout->addWidget(captionValueLabel);
    captionLayout->addStretch();
    basicInfoLayout->addLayout(captionLayout);

    // Position
    QHBoxLayout* posLayout = new QHBoxLayout();
    posLayout->setSpacing(12);

    QLabel* xLabel = new QLabel("X:");
    xLabel->setStyleSheet(QString("font-weight: bold; color: %1; min-width: 20px;").arg(primaryTextColor));
    m_xSpinBox = new QDoubleSpinBox();
    m_xSpinBox->setRange(-1e6, 1e6);
    m_xSpinBox->setDecimals(2);
    m_xSpinBox->setValue(m_nodeData.position.x());
    m_xSpinBox->setStyleSheet(QString(
        "QDoubleSpinBox {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  color: %3;"
        "}"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  border: none;"
        "  width: 16px;"
        "}"
    ).arg(inputBgColor).arg(inputBorderColor).arg(primaryTextColor));
    connect(m_xSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertyEditor::onPropertyValueChanged);
    posLayout->addWidget(xLabel);
    posLayout->addWidget(m_xSpinBox);

    QLabel* yLabel = new QLabel("Y:");
    yLabel->setStyleSheet(QString("font-weight: bold; color: %1; min-width: 20px;").arg(primaryTextColor));
    m_ySpinBox = new QDoubleSpinBox();
    m_ySpinBox->setRange(-1e6, 1e6);
    m_ySpinBox->setDecimals(2);
    m_ySpinBox->setValue(m_nodeData.position.y());
    m_ySpinBox->setStyleSheet(QString(
        "QDoubleSpinBox {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  color: %3;"
        "}"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  border: none;"
        "  width: 16px;"
        "}"
    ).arg(inputBgColor).arg(inputBorderColor).arg(primaryTextColor));
    connect(m_ySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertyEditor::onPropertyValueChanged);
    posLayout->addWidget(yLabel);
    posLayout->addWidget(m_ySpinBox);

    basicInfoLayout->addLayout(posLayout);

    // Execution state and progress (only for ExecutableNode)
    if (m_isExecutable) {
        QHBoxLayout* execLayout = new QHBoxLayout();
        execLayout->setSpacing(12);

        // Execution state
        QLabel* stateTitle = new QLabel("State:");
        stateTitle->setStyleSheet(QString("font-weight: bold; color: %1;").arg(primaryTextColor));
        m_executionStateLabel = new QLabel(executionStateToString(m_nodeData.executionState));

        // Color based on state
        QString stateColor = "#64748B";  // gray (default)
        switch (m_nodeData.executionState) {
            case QtNodes::ExecutionState::Idle: stateColor = darkTheme ? "#94A3B8" : "#94A3B8"; break;
            case QtNodes::ExecutionState::Pending: stateColor = "#10B981"; break;
            case QtNodes::ExecutionState::Running: stateColor = darkTheme ? "#4AA9CF" : "#3B82F6"; break;
            case QtNodes::ExecutionState::Completed: stateColor = "#10B981"; break;
            case QtNodes::ExecutionState::Stopped: stateColor = "#F59E0B"; break;
            case QtNodes::ExecutionState::Warning: stateColor = "#FBBF24"; break;
            case QtNodes::ExecutionState::Error: stateColor = "#EF4444"; break;
            case QtNodes::ExecutionState::Disabled: stateColor = "#94A3B8"; break;
        }
        m_executionStateLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(stateColor));

        // Execution mode
        QLabel* modeTitle = new QLabel("Mode:");
        modeTitle->setStyleSheet(QString("font-weight: bold; color: %1;").arg(primaryTextColor));
        m_modeLabel = new QLabel(executionModeToString(m_nodeData.executionMode));
        QString modeColor = (m_nodeData.executionMode == QtNodes::ExecutionMode::Automatic) ? "#3B82F6" : "#F59E0B";
        m_modeLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(modeColor));

        execLayout->addWidget(stateTitle);
        execLayout->addWidget(m_executionStateLabel);
        execLayout->addSpacing(16);
        execLayout->addWidget(modeTitle);
        execLayout->addWidget(m_modeLabel);
        execLayout->addStretch();

        basicInfoLayout->addLayout(execLayout);

        // Progress bar
        if (m_nodeData.executionState == QtNodes::ExecutionState::Running) {
            QLabel* progressLabel = new QLabel("Progress:");
            progressLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(primaryTextColor));
            basicInfoLayout->addWidget(progressLabel);

            m_progressBar = new QProgressBar();
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(m_nodeData.progress);
            m_progressBar->setTextVisible(true);
            m_progressBar->setFormat("%p%");

            QString progressBarBg = darkTheme ? "rgba(64, 64, 64, 0.8)" : "rgba(241, 245, 249, 0.9)";
            QString progressBarChunk = darkTheme ? "#4AA9CF" : "#3B82F6";
            QString progressBarBorder = darkTheme ? "rgba(148, 163, 184, 0.3)" : "#CBD5E1";

            m_progressBar->setStyleSheet(
                QString(
                    "QProgressBar {"
                    "   border: 1px solid %1;"
                    "   border-radius: 4px;"
                    "   background-color: %2;"
                    "   text-align: center;"
                    "   height: 20px;"
                    "   color: %3;"
                    "}"
                    "QProgressBar::chunk {"
                    "   background-color: %4;"
                    "   border-radius: 3px;"
                    "}"
                ).arg(progressBarBorder).arg(progressBarBg).arg(primaryTextColor).arg(progressBarChunk)
            );
            basicInfoLayout->addWidget(m_progressBar);
        }
    }

    // 添加到固定顶部区域（而不是 m_mainLayout）
    m_fixedTopLayout->addWidget(basicInfoWidget);

    // 添加分割条到基本信息下方（固定区域和滚动区域之间）
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    if (darkTheme) {
        separator->setStyleSheet("QFrame { background-color: rgba(255, 255, 255, 0.1); max-height: 1px; }");
    } else {
        separator->setStyleSheet("QFrame { background-color: #E2E8F0; max-height: 1px; }");
    }
    m_fixedTopLayout->addWidget(separator);
}

void PropertyEditor::generateInputSection()
{
    bool darkTheme = isDarkTheme();

    QVBoxLayout* contentLayout = qobject_cast<QVBoxLayout*>(m_inputSection.contentWidget->layout());
    if (!contentLayout) return;

    // Remove stretch to add content
    contentLayout->removeItem(contentLayout->itemAt(contentLayout->count() - 1));

    QString noPortsTextColor = darkTheme ? "#94A3B8" : "#94A3B8";
    QString sectionTextColor = darkTheme ? "#94A3B8" : "#64748B";
    QString sectionBgColor = darkTheme ? "rgba(64, 64, 64, 0.5)" : "rgba(241, 245, 249, 0.8)";

    bool hasContent = false;

    // Display input ports (from connected upstream nodes)
    if (!m_nodeData.inputPorts.isEmpty()) {
        QLabel* portsLabel = new QLabel("Port Data");
        portsLabel->setStyleSheet(QString("color: %1; font-weight: bold; margin-top: 8px; margin-bottom: 4px;").arg(sectionTextColor));
        contentLayout->addWidget(portsLabel);

        for (const PortDataInfo& info : m_nodeData.inputPorts) {
            // Input ports are generally not editable (data comes from connected nodes)
            addPortCard(contentLayout, info, false);
        }
        hasContent = true;
    }

    // Display widget parameters (from node's controls like QLineEdit)
    if (!m_nodeData.parameters.isEmpty()) {
        if (hasContent) {
            // Add separator
            QLabel* separator = new QLabel();
            separator->setStyleSheet(QString("background-color: %1; margin: 12px 0; max-height: 1px;").arg(sectionBgColor));
            contentLayout->addWidget(separator);
        }

        QLabel* paramsLabel = new QLabel("Node Parameters");
        paramsLabel->setStyleSheet(QString("color: %1; font-weight: bold; margin-top: 8px; margin-bottom: 4px;").arg(sectionTextColor));
        contentLayout->addWidget(paramsLabel);

        for (const QtNodes::ParameterInfo& param : m_nodeData.parameters) {
            addParameterCard(contentLayout, param);
        }
        hasContent = true;
    }

    if (!hasContent) {
        QLabel* noContentLabel = new QLabel("No input ports or parameters");
        noContentLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(noPortsTextColor));
        contentLayout->addWidget(noContentLabel);
    }
    // 不再添加 stretch，区域按实际内容大小排列
}

void PropertyEditor::generateProcessingSection()
{
    bool darkTheme = isDarkTheme();

    QVBoxLayout* contentLayout = qobject_cast<QVBoxLayout*>(m_processingSection.contentWidget->layout());
    if (!contentLayout) return;

    // Remove stretch to add content
    contentLayout->removeItem(contentLayout->itemAt(contentLayout->count() - 1));

    QString infoTextColor = darkTheme ? "#FFFFFF" : "#1E3A8A";
    QString infoBg = darkTheme ? "rgba(64, 64, 64, 0.6)" : "rgba(241, 245, 249, 0.6)";
    QString noInfoTextColor = darkTheme ? "#94A3B8" : "#94A3B8";

    if (m_nodeData.processingInfo.isEmpty()) {
        QLabel* noInfoLabel = new QLabel("No processing info available");
        noInfoLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(noInfoTextColor));
        contentLayout->addWidget(noInfoLabel);
    } else {
        for (const QString& info : m_nodeData.processingInfo) {
            QLabel* infoLabel = new QLabel(info);
            infoLabel->setWordWrap(true);
            infoLabel->setStyleSheet(QString(
                "QLabel {"
                "  color: %1;"
                "  background-color: %2;"
                "  padding: 8px 10px;"
                "  border-radius: 4px;"
                "  border-left: 3px solid #3B82F6;"
                "}"
            ).arg(infoTextColor).arg(infoBg));
            contentLayout->addWidget(infoLabel);
        }
    }
    // 不再添加 stretch，区域按实际内容大小排列
}

void PropertyEditor::generateOutputSection()
{
    bool darkTheme = isDarkTheme();

    QVBoxLayout* contentLayout = qobject_cast<QVBoxLayout*>(m_outputSection.contentWidget->layout());
    if (!contentLayout) return;

    // Remove stretch to add content
    contentLayout->removeItem(contentLayout->itemAt(contentLayout->count() - 1));

    QString noPortsTextColor = darkTheme ? "#94A3B8" : "#94A3B8";

    if (m_nodeData.outputPorts.isEmpty()) {
        QLabel* noPortsLabel = new QLabel("No output ports");
        noPortsLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(noPortsTextColor));
        contentLayout->addWidget(noPortsLabel);
    } else {
        for (const PortDataInfo& info : m_nodeData.outputPorts) {
            // Output ports may be editable for source nodes
            // For now, mark as not editable
            addPortCard(contentLayout, info, false);
        }
    }
    // 不再添加 stretch，区域按实际内容大小排列
}

void PropertyEditor::addPortCard(QVBoxLayout* layout, const PortDataInfo& info, bool isEditable)
{
    bool darkTheme = isDarkTheme();

    // Create card with glass effect
    QFrame* card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);

    QString cardBg = darkTheme ? "rgba(64, 64, 64, 0.5)" : "rgba(255, 255, 255, 0.7)";
    QString cardBorder = darkTheme ? "rgba(148, 163, 184, 0.3)" : "rgba(148, 163, 184, 0.3)";

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

    // Text colors based on theme
    QString primaryTextColor = darkTheme ? "#FFFFFF" : "#1E3A8A";
    QString secondaryTextColor = darkTheme ? "#94A3B8" : "#64748B";
    QString tertiaryTextColor = darkTheme ? "#FFFFFF" : "#334155";

    // Port name with optional index
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

    // Data type
    QLabel* typeLabel = new QLabel(QString("Type: %1").arg(info.dataType));
    typeLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(secondaryTextColor));
    cardLayout->addWidget(typeLabel);

    // Check if summary and fields are redundant
    bool showSummary = !info.summary.isEmpty();
    if (showSummary && !info.fields.isEmpty()) {
        // If fields has only one field and its value is similar to summary, skip summary
        if (info.fields.size() == 1) {
            const auto& firstField = info.fields.first();
            // Compare summary with field value (case-insensitive, trimmed)
            QString summaryTrimmed = info.summary.trimmed().toLower();
            QString valueTrimmed = firstField.value.trimmed().toLower();
            if (summaryTrimmed == valueTrimmed) {
                showSummary = false;
            }
        }
    }

    // Summary (if exists and not redundant)
    if (showSummary) {
        QString displaySummary = info.summary;
        // Truncate long values
        if (displaySummary.length() > 60) {
            displaySummary = displaySummary.left(60) + "...";
        }

        QLabel* summaryLabel = new QLabel(displaySummary);
        QString summaryBg = darkTheme ? "rgba(64, 64, 64, 0.6)" : "rgba(241, 245, 249, 0.5)";
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

    // Fields (if exists) - 支持可编辑
    if (!info.fields.isEmpty()) {
        for (const auto& field : info.fields) {
            // 创建字段行
            QHBoxLayout* fieldLayout = new QHBoxLayout();
            fieldLayout->setSpacing(6);

            QLabel* keyLabel = new QLabel(field.key + ":");
            keyLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(secondaryTextColor));
            keyLabel->setMinimumWidth(60);
            fieldLayout->addWidget(keyLabel);

            // 根据编辑类型和 isEditable 创建不同的控件
            if (!isEditable || field.editType == QtNodes::FieldEditType::None) {
                // 只读标签
                QString displayValue = field.value;
                if (displayValue.length() > 50) {
                    displayValue = displayValue.left(50) + "...";
                }
                QLabel* valueLabel = new QLabel(displayValue);
                valueLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(tertiaryTextColor));
                valueLabel->setWordWrap(true);
                fieldLayout->addWidget(valueLabel);
            }
            else if (field.editType == QtNodes::FieldEditType::Text) {
                // 只读标签
                QString displayValue = field.value;
                if (displayValue.length() > 50) {
                    displayValue = displayValue.left(50) + "...";
                }
                QLabel* valueLabel = new QLabel(displayValue);
                valueLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(tertiaryTextColor));
                valueLabel->setWordWrap(true);
                fieldLayout->addWidget(valueLabel);
            }
            else if (field.editType == QtNodes::FieldEditType::Text) {
                // 文本编辑框
                QLineEdit* lineEdit = new QLineEdit(field.value);
                lineEdit->setStyleSheet(QString(
                    "QLineEdit {"
                    "   color: %1;"
                    "   font-size: 10px;"
                    "   background-color: rgba(255, 255, 255, 0.1);"
                    "   border: 1px solid rgba(148, 163, 184, 0.3);"
                    "   border-radius: 4px;"
                    "   padding: 3px 6px;"
                    "}"
                    "QLineEdit:focus {"
                    "   border-color: #3B82F6;"
                    "}"
                ).arg(tertiaryTextColor));

                // 连接编辑完成信号
                connect(lineEdit, &QLineEdit::editingFinished, this, [this, lineEdit, info, field]() {
                    QString newValue = lineEdit->text();
                    if (newValue != field.value) {
                        emit portDataChanged(m_currentNodeId, info.portType, info.index, field.key, newValue);
                    }
                });

                fieldLayout->addWidget(lineEdit);
            }
            else if (field.editType == QtNodes::FieldEditType::Number) {
                // 数字编辑框
                QDoubleSpinBox* spinBox = new QDoubleSpinBox();
                spinBox->setRange(field.minNumber, field.maxNumber);
                spinBox->setDecimals(field.decimals);
                spinBox->setValue(field.value.toDouble());
                spinBox->setStyleSheet(QString(
                    "QDoubleSpinBox {"
                    "   color: %1;"
                    "   font-size: 10px;"
                    "   background-color: rgba(255, 255, 255, 0.1);"
                    "   border: 1px solid rgba(148, 163, 184, 0.3);"
                    "   border-radius: 4px;"
                    "   padding: 2px 6px;"
                    "}"
                ).arg(tertiaryTextColor));

                // 连接值变化信号
                connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this, [this, info, field](double value) {
                    QString newValue = QString::number(value, 'f', field.decimals);
                    emit portDataChanged(m_currentNodeId, info.portType, info.index, field.key, newValue);
                });

                fieldLayout->addWidget(spinBox);
            }
            else if (field.editType == QtNodes::FieldEditType::Path) {
                // 文件路径选择
                QHBoxLayout* pathLayout = new QHBoxLayout();
                pathLayout->setSpacing(4);

                QLineEdit* lineEdit = new QLineEdit(field.value);
                lineEdit->setStyleSheet(QString(
                    "QLineEdit {"
                    "   color: %1;"
                    "   font-size: 10px;"
                    "   background-color: rgba(255, 255, 255, 0.1);"
                    "   border: 1px solid rgba(148, 163, 184, 0.3);"
                    "   border-radius: 4px;"
                    "   padding: 3px 6px;"
                    "}"
                ).arg(tertiaryTextColor));

                QToolButton* browseBtn = new QToolButton();
                browseBtn->setText("...");
                browseBtn->setStyleSheet(QString(
                    "QToolButton {"
                    "   background-color: rgba(59, 130, 246, 0.3);"
                    "   border: 1px solid rgba(59, 130, 246, 0.5);"
                    "   border-radius: 4px;"
                    "   min-width: 24px;"
                    "   max-width: 24px;"
                    "}"
                    "QToolButton:hover {"
                    "   background-color: rgba(59, 130, 246, 0.5);"
                    "}"
                ).arg(tertiaryTextColor));

                // 浏览按钮点击事件
                connect(browseBtn, &QToolButton::clicked, this, [this, lineEdit, field]() {
                    QString fileName = QFileDialog::getOpenFileName(
                        this,
                        "Select File",
                        lineEdit->text(),
                        field.pathFilter
                    );
                    if (!fileName.isEmpty()) {
                        lineEdit->setText(fileName);
                    }
                });

                // 连接编辑完成信号
                connect(lineEdit, &QLineEdit::editingFinished, this, [this, lineEdit, info, field]() {
                    QString newValue = lineEdit->text();
                    if (newValue != field.value) {
                        emit portDataChanged(m_currentNodeId, info.portType, info.index, field.key, newValue);
                    }
                });

                pathLayout->addWidget(lineEdit, 1);
                pathLayout->addWidget(browseBtn);
                fieldLayout->addLayout(pathLayout);
            }

            cardLayout->addLayout(fieldLayout);
        }
    }

    // Connection status with color coding (using QChar for better Unicode support)
    QString statusText = info.isConnected ?
        QString(QChar(0x25CF)) + " Connected" :
        QString(QChar(0x25CB)) + " Not connected";
    QString statusColor = info.isConnected ? "#10B981" : "#94A3B8";
    QLabel* statusLabel = new QLabel(statusText);
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(statusColor));
    cardLayout->addWidget(statusLabel);

    layout->addWidget(card);
}

void PropertyEditor::addParameterCard(QVBoxLayout* layout, const QtNodes::ParameterInfo& param)
{
    bool darkTheme = isDarkTheme();

    // Create card with glass effect
    QFrame* card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);

    QString cardBg = darkTheme ? "rgba(64, 64, 64, 0.5)" : "rgba(255, 255, 255, 0.7)";
    QString cardBorder = darkTheme ? "rgba(148, 163, 184, 0.3)" : "rgba(148, 163, 184, 0.3)";

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

    // Text colors based on theme
    QString primaryTextColor = darkTheme ? "#FFFFFF" : "#1E3A8A";
    QString secondaryTextColor = darkTheme ? "#94A3B8" : "#64748B";
    QString tertiaryTextColor = darkTheme ? "#FFFFFF" : "#334155";

    // Parameter name with bold styling
    QLabel* nameLabel = new QLabel(QString("<b>%1</b>").arg(param.name));
    nameLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(primaryTextColor));
    cardLayout->addWidget(nameLabel);

    // Data type
    QLabel* typeLabel = new QLabel(QString("Type: %1").arg(param.dataType));
    typeLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(secondaryTextColor));
    cardLayout->addWidget(typeLabel);

    // Create editable control based on editType
    if (param.editType == QtNodes::FieldEditType::Text) {
        QLineEdit* edit = new QLineEdit(param.value);
        edit->setStyleSheet(QString(
            "QLineEdit {"
            "   color: %1;"
            "   font-size: 11px;"
            "   background-color: rgba(255, 255, 255, 0.1);"
            "   border: 1px solid rgba(148, 163, 184, 0.3);"
            "   border-radius: 4px;"
            "   padding: 4px 8px;"
            "}"
            "QLineEdit:hover {"
            "   border: 1px solid rgba(59, 130, 246, 0.5);"
            "}"
            "QLineEdit:focus {"
            "   border: 1px solid #3B82F6;"
            "}"
        ).arg(tertiaryTextColor));

        // Connect editing finished signal to update parameter
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, param]() {
            QString newValue = edit->text();
            if (newValue != param.value) {
                emit propertyChanged(m_currentNodeId, param.name, newValue);
                // TODO: Call a method on the node to update the parameter value
                // This requires extending ExecutableNodeDelegateModel with setParameter()
            }
        });

        cardLayout->addWidget(edit);
    } else if (param.editType == QtNodes::FieldEditType::Number) {
        QDoubleSpinBox* spinBox = new QDoubleSpinBox();
        spinBox->setRange(param.minNumber, param.maxNumber);
        spinBox->setDecimals(param.decimals);
        spinBox->setValue(param.value.toDouble());
        spinBox->setStyleSheet(QString(
            "QDoubleSpinBox {"
            "   color: %1;"
            "   font-size: 11px;"
            "   background-color: rgba(255, 255, 255, 0.1);"
            "   border: 1px solid rgba(148, 163, 184, 0.3);"
            "   border-radius: 4px;"
            "   padding: 2px 4px;"
            "}"
            "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
            "   width: 16px;"
            "}"
        ).arg(tertiaryTextColor));

        // Connect value changed signal to update parameter
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, spinBox, param](double value) {
            QString newValue = QString::number(value, 'f', param.decimals);
            emit propertyChanged(m_currentNodeId, param.name, newValue);
            // TODO: Call a method on the node to update the parameter value
        });

        cardLayout->addWidget(spinBox);
    } else if (param.editType == QtNodes::FieldEditType::Path) {
        QHBoxLayout* pathLayout = new QHBoxLayout();
        pathLayout->setSpacing(6);

        QLineEdit* edit = new QLineEdit(param.value);
        edit->setStyleSheet(QString(
            "QLineEdit {"
            "   color: %1;"
            "   font-size: 11px;"
            "   background-color: rgba(255, 255, 255, 0.1);"
            "   border: 1px solid rgba(148, 163, 184, 0.3);"
            "   border-radius: 4px;"
            "   padding: 4px 8px;"
            "}"
        ).arg(tertiaryTextColor));

        QToolButton* browseBtn = new QToolButton();
        browseBtn->setText("...");
        browseBtn->setStyleSheet(QString(
            "QToolButton {"
            "   background-color: rgba(59, 130, 246, 0.3);"
            "   border: 1px solid rgba(59, 130, 246, 0.5);"
            "   border-radius: 4px;"
            "   min-width: 24px;"
            "   max-width: 24px;"
            "}"
            "QToolButton:hover {"
            "   background-color: rgba(59, 130, 246, 0.5);"
            "}"
        ));

        // Connect browse button to file dialog
        connect(browseBtn, &QToolButton::clicked, this, [this, edit, param]() {
            QString fileName = QFileDialog::getOpenFileName(
                this,
                QString("Select %1").arg(param.name),
                edit->text(),
                param.pathFilter
            );
            if (!fileName.isEmpty()) {
                edit->setText(fileName);
                emit propertyChanged(m_currentNodeId, param.name, fileName);
                // TODO: Call a method on the node to update the parameter value
            }
        });

        // Connect editing finished signal
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, param]() {
            QString newValue = edit->text();
            if (newValue != param.value) {
                emit propertyChanged(m_currentNodeId, param.name, newValue);
                // TODO: Call a method on the node to update the parameter value
            }
        });

        pathLayout->addWidget(edit, 1);
        pathLayout->addWidget(browseBtn);
        cardLayout->addLayout(pathLayout);
    } else {
        // Read-only display
        QString displayValue = param.value;
        if (displayValue.length() > 60) {
            displayValue = displayValue.left(60) + "...";
        }

        QLabel* valueLabel = new QLabel(displayValue);
        QString valueBg = darkTheme ? "rgba(64, 64, 64, 0.6)" : "rgba(241, 245, 249, 0.5)";
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
    }

    layout->addWidget(card);
}

void PropertyEditor::clearProperties()
{
    // Clear collapsible section content
    auto clearSectionContent = [](CollapsibleSection& section) {
        if (section.contentWidget) {
            QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(section.contentWidget->layout());
            if (layout) {
                while (QLayoutItem* item = layout->takeAt(0)) {
                    if (item->widget()) {
                        item->widget()->deleteLater();
                    }
                    delete item;
                }
                // 不再添加 stretch，区域按实际内容大小排列
            }
        }
    };

    clearSectionContent(m_inputSection);
    clearSectionContent(m_processingSection);
    clearSectionContent(m_outputSection);
}

void PropertyEditor::clearBasicInfoFromLayout()
{
    if (!m_fixedTopLayout)
        return;

    // 清除 m_fixedTopLayout 中的 separator 和 basicInfoWidget
    // 跳过 m_noSelectionLabel
    for (int i = m_fixedTopLayout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = m_fixedTopLayout->itemAt(i);
        if (!item) continue;

        QWidget* widget = item->widget();
        if (!widget) continue;

        // 跳过 m_noSelectionLabel
        if (widget == m_noSelectionLabel) {
            continue;
        }

        // Remove and delete the widget (separator or basicInfoWidget)
        m_fixedTopLayout->removeWidget(widget);
        widget->deleteLater();
    }
}

void PropertyEditor::captureNodeData(QtNodes::NodeId nodeId)
{
    if (!m_graphModel || nodeId == QtNodes::InvalidNodeId) {
        m_isExecutable = false;
        return;
    }

    // Clear old data before capturing new data
    m_nodeData.inputPorts.clear();
    m_nodeData.outputPorts.clear();
    m_nodeData.processingInfo.clear();

    // Capture basic node info
    m_nodeData.caption = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Caption).toString();
    m_nodeData.position = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).value<QPointF>();

    // Try to get ExecutableNodeDelegateModel
    auto execModel = m_graphModel->delegateModel<QtNodes::ExecutableNodeDelegateModel>(nodeId);
    m_isExecutable = (execModel != nullptr);

    if (m_isExecutable && execModel) {
        // Capture execution state and progress
        m_nodeData.executionState = execModel->executionState();
        m_nodeData.executionMode = execModel->executionMode();
        m_nodeData.progress = execModel->progress();

        // Capture input ports
        int inputPortCount = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::InPortCount).toInt();
        for (int i = 0; i < inputPortCount; ++i) {
            PortDataInfo info;
            info.index = i;
            info.portType = QtNodes::PortType::In;
            QString caption = m_graphModel->portData(nodeId, QtNodes::PortType::In, i, QtNodes::PortRole::Caption).toString();
            if (caption.isEmpty()) {
                // Use dataType name as port name (e.g., "In Data")
                auto portDataType = execModel->dataType(QtNodes::PortType::In, i);
                info.name = portDataType.name;
                // No custom caption: show index only if there are multiple ports
                info.showIndex = (inputPortCount > 1);
            } else {
                // Has custom caption: use it directly, no index needed
                info.name = caption;
                info.showIndex = false;
            }
            info.dataType = "";
            info.summary = "";
            info.fields = {};
            info.isConnected = !m_graphModel->connections(nodeId, QtNodes::PortType::In, i).empty();

            if (info.isConnected && execModel) {
                auto data = execModel->getInputData(i);
                if (data) {
                    // Get type name directly from data object
                    info.dataType = data->type().name;
                    info.summary = data->getSummary();
                    info.fields = data->getFields();
                }
            }

            m_nodeData.inputPorts.append(info);
        }

        // Capture output ports
        int outputPortCount = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::OutPortCount).toInt();
        for (int i = 0; i < outputPortCount; ++i) {
            PortDataInfo info;
            info.index = i;
            info.portType = QtNodes::PortType::Out;
            QString caption = m_graphModel->portData(nodeId, QtNodes::PortType::Out, i, QtNodes::PortRole::Caption).toString();
            if (caption.isEmpty()) {
                // Use dataType name as port name (e.g., "Out Data")
                auto portDataType = execModel->dataType(QtNodes::PortType::Out, i);
                info.name = portDataType.name;
                // No custom caption: show index only if there are multiple ports
                info.showIndex = (outputPortCount > 1);
            } else {
                // Has custom caption: use it directly, no index needed
                info.name = caption;
                info.showIndex = false;
            }
            info.dataType = "";
            info.summary = "";
            info.fields = {};
            info.isConnected = !m_graphModel->connections(nodeId, QtNodes::PortType::Out, i).empty();

            if (execModel) {
                auto data = execModel->getOutputData(i);
                if (data) {
                    // Get type name directly from data object
                    info.dataType = data->type().name;
                    info.summary = data->getSummary();
                    info.fields = data->getFields();
                }
            }

            m_nodeData.outputPorts.append(info);
        }

        // Capture widget parameters (from node's controls like QLineEdit, etc.)
        m_nodeData.parameters = execModel->getParameters();

        // Processing info is cleared (no status/mode info since shown in Basic Info section)
        // This section is reserved for future use (e.g., processing logs, messages, etc.)
        m_nodeData.processingInfo.clear();
    } else {
        // Non-executable node - just capture port metadata
        int inputPortCount = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::InPortCount).toInt();
        for (int i = 0; i < inputPortCount; ++i) {
            PortDataInfo info;
            info.index = i;
            info.portType = QtNodes::PortType::In;
            info.name = m_graphModel->portData(nodeId, QtNodes::PortType::In, i, QtNodes::PortRole::Caption).toString();
            if (info.name.isEmpty()) {
                // Use dataType name as port name
                auto portDataType = m_graphModel->portData(nodeId, QtNodes::PortType::In, i, QtNodes::PortRole::DataType).value<QtNodes::NodeDataType>();
                info.name = portDataType.name;
            }
            info.dataType = m_graphModel->portData(nodeId, QtNodes::PortType::In, i, QtNodes::PortRole::DataType).value<QtNodes::NodeDataType>().name;
            info.summary = "";
            info.fields = {};
            info.isConnected = !m_graphModel->connections(nodeId, QtNodes::PortType::In, i).empty();
            m_nodeData.inputPorts.append(info);
        }

        int outputPortCount = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::OutPortCount).toInt();
        for (int i = 0; i < outputPortCount; ++i) {
            PortDataInfo info;
            info.index = i;
            info.portType = QtNodes::PortType::Out;
            info.name = m_graphModel->portData(nodeId, QtNodes::PortType::Out, i, QtNodes::PortRole::Caption).toString();
            if (info.name.isEmpty()) {
                // Use dataType name as port name
                auto portDataType = m_graphModel->portData(nodeId, QtNodes::PortType::Out, i, QtNodes::PortRole::DataType).value<QtNodes::NodeDataType>();
                info.name = portDataType.name;
            }
            info.dataType = m_graphModel->portData(nodeId, QtNodes::PortType::Out, i, QtNodes::PortRole::DataType).value<QtNodes::NodeDataType>().name;
            info.summary = "";
            info.fields = {};
            info.isConnected = !m_graphModel->connections(nodeId, QtNodes::PortType::Out, i).empty();
            m_nodeData.outputPorts.append(info);
        }

        m_nodeData.executionState = QtNodes::ExecutionState::Idle;
        m_nodeData.executionMode = QtNodes::ExecutionMode::Automatic;
        m_nodeData.progress = 0;
        // Processing info is cleared (reserved for future use)
        m_nodeData.processingInfo.clear();
    }
}

QString PropertyEditor::executionStateToString(QtNodes::ExecutionState state) const
{
    switch (state) {
        case QtNodes::ExecutionState::Idle: return "Idle";
        case QtNodes::ExecutionState::Pending: return "Pending";
        case QtNodes::ExecutionState::Running: return "Running";
        case QtNodes::ExecutionState::Completed: return "Completed";
        case QtNodes::ExecutionState::Stopped: return "Stopped";
        case QtNodes::ExecutionState::Warning: return "Warning";
        case QtNodes::ExecutionState::Error: return "Error";
        case QtNodes::ExecutionState::Disabled: return "Disabled";
        default: return "Unknown";
    }
}

QString PropertyEditor::executionModeToString(QtNodes::ExecutionMode mode) const
{
    switch (mode) {
        case QtNodes::ExecutionMode::Automatic: return "Automatic";
        case QtNodes::ExecutionMode::Manual: return "Manual";
        default: return "Unknown";
    }
}

void PropertyEditor::onPropertyValueChanged()
{
    if (m_updatingProperties || !m_graphModel || m_currentNodeId == QtNodes::InvalidNodeId)
        return;

    m_updatingProperties = true;

    // Update position
    if (m_xSpinBox && m_ySpinBox)
    {
        QPointF pos(m_xSpinBox->value(), m_ySpinBox->value());
        m_graphModel->setNodeData(m_currentNodeId, QtNodes::NodeRole::Position, pos);
    }

    // Emit signal for property change notification
    emit propertyChanged(m_currentNodeId, "basic", QVariant());

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
    layout->addWidget(label);

    QLabel *placeholder = new QLabel("Queue functionality coming soon...");
    placeholder->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholder);

    layout->addStretch();
}
