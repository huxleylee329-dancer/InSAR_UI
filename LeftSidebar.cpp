#include "include/LeftSidebar.h"
#include "NodeModels.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QList>
#include <QMap>
#include <QPair>
#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QStyle>

#include <QtNodes/NodeDelegateModelRegistry>

// WorkflowBrowser implementation
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
        QString name = item->text(0);
        bool match = text.isEmpty() || name.contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        ++it;
    }
}

void WorkflowBrowser::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty())
    {
        emit loadWorkflow(filePath);
    }
}

// LeftSidebar implementation
LeftSidebar::LeftSidebar(QWidget *parent)
    : QWidget(parent)
    , m_tabWidget(nullptr)
    , m_searchBox(nullptr)
    , m_nodeTree(nullptr)
    , m_workflowBrowser(nullptr)
    , m_workflowPath()
    , m_collapsed(false)
    , m_normalWidth(250)
{
    setupUi();
}

LeftSidebar::~LeftSidebar()
{
    // Child widgets are automatically deleted by Qt
}

void LeftSidebar::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::North);
    m_tabWidget->setDocumentMode(true);

    // Add tabs
    setupNodeLibraryTab();
    setupWorkflowsTab();

    mainLayout->addWidget(m_tabWidget);

    // Set size
    setMinimumWidth(200);
    setMaximumWidth(300);
}

void LeftSidebar::toggleCollapse()
{
    m_collapsed = !m_collapsed;

    if (m_collapsed)
    {
        m_normalWidth = width();
        setFixedWidth(30);
        m_tabWidget->hide();
    }
    else
    {
        m_tabWidget->show();
        setFixedWidth(m_normalWidth);
        setMinimumWidth(200);
        setMaximumWidth(300);
    }
}

void LeftSidebar::setupNodeLibraryTab()
{
    auto *tabWidget = new QWidget();
    auto *tabLayout = new QVBoxLayout(tabWidget);
    tabLayout->setContentsMargins(4, 4, 4, 4);
    tabLayout->setSpacing(4);

    // Search box
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search nodes...");
    m_searchBox->setClearButtonEnabled(true);
    tabLayout->addWidget(m_searchBox);

    // Node tree
    m_nodeTree = new NodeTreeWidget();
    m_nodeTree->setHeaderHidden(true);
    m_nodeTree->setIndentation(12);
    m_nodeTree->setSortingEnabled(false);  // Keep creation order, not alphabetical
    tabLayout->addWidget(m_nodeTree);

    // Connect signals
    connect(m_searchBox, &QLineEdit::textChanged,
            this, &LeftSidebar::onNodeSearchTextChanged);
    connect(m_nodeTree, &NodeTreeWidget::leafItemDoubleClicked,
            this, &LeftSidebar::onNodeItemDoubleClicked);
    connect(m_nodeTree, &QTreeWidget::itemClicked,
            this, &LeftSidebar::onNodeItemClicked);

    // Add tab
    m_tabWidget->addTab(tabWidget, "Nodes");
}

void LeftSidebar::setupWorkflowsTab()
{
    m_workflowBrowser = new WorkflowBrowser();
    m_workflowBrowser->setWorkflowPath(m_workflowPath);

    // Connect load workflow signal
    connect(m_workflowBrowser, &WorkflowBrowser::loadWorkflow,
            this, &LeftSidebar::workflowLoadRequested);

    // Add tab
    m_tabWidget->addTab(m_workflowBrowser, "Workflows");
}

void LeftSidebar::setRegistry(std::shared_ptr<QtNodes::NodeDelegateModelRegistry> registry)
{
    m_registry = registry;
    populateNodeTree();
}

void LeftSidebar::populateNodeTree()
{
    if (!m_registry)
        return;

    m_nodeTree->clear();

    auto models = m_registry->registeredModelsCategoryAssociation();

    // Group models by their paths
    // Map: path -> list of (modelName, leafName) or (modelName, modelName) for 1-level
    QMap<QString, QList<QPair<QString, QString>>> pathModels;

    for (const auto &pair : models)
    {
        const QString &modelName = pair.first;
        const QString &categoryPath = pair.second;

        QStringList parts = categoryPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        if (parts.size() == 1)
        {
            // 1-level: path is category, leafName is modelName
            pathModels[categoryPath].append(qMakePair(modelName, modelName));
        }
        else
        {
            // Multi-level: last part is leaf, rest is subcategory path
            QString subcategory = QStringList(parts.mid(0, parts.size() - 1)).join('/');
            QString leafName = parts.last();
            pathModels[subcategory].append(qMakePair(modelName, leafName));
        }
    }

    // Helper function to find or create a tree item
    auto findOrCreateItem = [this](QTreeWidgetItem *parent, const QString &text, bool isLeaf) -> QTreeWidgetItem* {
        QTreeWidgetItem *targetParent = parent ? parent : nullptr;

        int count = targetParent ? targetParent->childCount() : m_nodeTree->topLevelItemCount();
        for (int i = 0; i < count; ++i) {
            QTreeWidgetItem *item = targetParent ? targetParent->child(i) : m_nodeTree->topLevelItem(i);
            if (item->text(0) == text) {
                return item;
            }
        }

        QTreeWidgetItem *newItem = targetParent ? new QTreeWidgetItem(targetParent) : new QTreeWidgetItem(m_nodeTree);
        newItem->setText(0, text);
        newItem->setExpanded(!isLeaf);
        newItem->setFlags(isLeaf ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::ItemIsEnabled);
        return newItem;
    };

    // Process all paths
    for (auto it = pathModels.constBegin(); it != pathModels.constEnd(); ++it)
    {
        const QString &path = it.key();
        const QList<QPair<QString, QString>> &modelsList = it.value();

        QStringList parts = path.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        // Find or create tree items for all levels
        QTreeWidgetItem *currentParent = nullptr;
        for (int i = 0; i < parts.size(); ++i)
        {
            bool isLeaf = (i == parts.size() - 1);
            currentParent = findOrCreateItem(currentParent, parts[i], isLeaf && modelsList.isEmpty());
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

void LeftSidebar::onNodeSearchTextChanged(const QString &text)
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
        // Category items (items with children)
        else
        {
            // Categories are always shown if they have any visible children
            // We'll determine this after processing all items
            if (text.isEmpty())
            {
                item->setHidden(false);
                // Expand all by default when search is empty
                item->setExpanded(true);
            }
            else
            {
                // During search, expand to show matching children
                item->setExpanded(true);
            }
        }

        ++it;
    }

    // Second pass: hide categories that have no visible children
    if (!text.isEmpty())
    {
        QTreeWidgetItemIterator it2(m_nodeTree);
        while (*it2)
        {
            QTreeWidgetItem *item = *it2;
            if (item->childCount() > 0)
            {
                bool hasVisibleChildren = false;
                for (int i = 0; i < item->childCount(); ++i)
                {
                    if (!item->child(i)->isHidden())
                    {
                        hasVisibleChildren = true;
                        break;
                    }
                }
                item->setHidden(!hasVisibleChildren);
            }
            ++it2;
        }
    }

    emit nodeSearchTextChanged(text);
}

void LeftSidebar::onNodeItemDoubleClicked(const QString &modelName)
{
    emit nodeDoubleClicked(modelName);
}

void LeftSidebar::onNodeItemClicked(QTreeWidgetItem *item, int column)
{
    if (item->childCount() == 0)
    {
        QString modelName = item->data(0, Qt::UserRole).toString();
        emit nodeItemClicked(modelName);
    }
}
