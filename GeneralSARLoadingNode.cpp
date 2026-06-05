#include "GeneralSARLoadingNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "NodeUtils.h"
#include "ImportDataTypes.h"
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QShowEvent>

namespace QtNodes {

class LoadingNodeWidget : public QWidget
{
public:
    explicit LoadingNodeWidget(GeneralSARLoadingNode* node, QWidget* parent = nullptr)
        : QWidget(parent), m_node(node) {}
protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (m_node) {
            m_node->onProjectModelChanged(m_node->projectModel());
        }
    }
private:
    GeneralSARLoadingNode* m_node;
};

GeneralSARLoadingNode::GeneralSARLoadingNode()
    : ImportNodeBase()
    , m_projectNameLabel(nullptr)
    , m_loadingNodeCombo(nullptr)
    , m_fileListWidget(nullptr)
    , m_isNewNode(true)
{
}

QWidget* GeneralSARLoadingNode::createWidget()
{
    auto* widget = new LoadingNodeWidget(this);
    widget->setFixedWidth(300);
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    // Project Row
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel(QStringLiteral("项目名称：")));
    m_projectNameLabel = new QLabel();
    m_projectNameLabel->setText(projectName().isEmpty() ? QStringLiteral("未打开项目") : projectName());
    projectRow->addWidget(m_projectNameLabel);
    mainLayout->addLayout(projectRow);

    // Loading Node Row
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel(QStringLiteral("装入节点：")));
    m_loadingNodeCombo = new PopupComboBox();
    m_loadingNodeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_loadingNodeCombo->setEditable(false);
    nodeRow->addWidget(m_loadingNodeCombo);
    mainLayout->addLayout(nodeRow);

    // File List Widget
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    m_fileListWidget->setSelectionMode(QAbstractItemView::MultiSelection);
    mainLayout->addWidget(m_fileListWidget);

    // Connections
    connect(m_loadingNodeCombo, &PopupComboBox::aboutToShowPopup, this, &GeneralSARLoadingNode::refreshNodeList);

    connect(m_loadingNodeCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (text.isEmpty()) return;
        m_loadingNodeName = text;
        m_checkedFilePaths.clear();
        m_isNewNode = true;
        refreshUI();
        setState(ExecutionState::Idle);
    });

    connect(m_fileListWidget, &QListWidget::itemSelectionChanged, this, &GeneralSARLoadingNode::onSelectionChanged);

    // Connect to MainWindow sendModel signal to detect project loading/switching
    MainWindow* mainWin = nullptr;
    for (QWidget* topLevelWidget : QApplication::topLevelWidgets()) {
        mainWin = qobject_cast<MainWindow*>(topLevelWidget);
        if (mainWin) {
            break;
        }
    }

    if (mainWin) {
        connect(mainWin, &MainWindow::sendModel, this, &GeneralSARLoadingNode::onProjectModelChanged, Qt::UniqueConnection);
    }

    // Initialize UI values and connect to the active project model signals
    onProjectModelChanged(projectModel());

    return widget;
}

void GeneralSARLoadingNode::refreshUI()
{
    if (!_widget) return;

    // Update project name
    if (m_projectNameLabel) {
        m_projectNameLabel->setText(projectName().isEmpty() ? QStringLiteral("未打开项目") : projectName());
    }

    QStandardItemModel* model = projectModel();
    if (!model) {
        m_loadingNodeCombo->clear();
        m_fileListWidget->clear();
        return;
    }

    m_loadingNodeCombo->blockSignals(true);
    m_fileListWidget->blockSignals(true);

    // Remember current selected node name
    QString currentSelectedNode = m_loadingNodeCombo->currentText();
    if (currentSelectedNode.isEmpty()) {
        currentSelectedNode = m_loadingNodeName;
    }

    m_loadingNodeCombo->clear();

    QList<QStandardItem*> projItems = model->findItems(projectName());
    if (projItems.isEmpty() && model->rowCount() > 0) {
        projItems.append(model->item(0, 0));
    }

    QStandardItem* selectedNodeItem = nullptr;
    if (!projItems.isEmpty()) {
        QStandardItem* projItem = projItems.first();
        for (int i = 0; i < projItem->rowCount(); ++i) {
            QStandardItem* nodeItem = projItem->child(i, 0);
            if (nodeItem) {
                QString nodeName = nodeItem->text();
                m_loadingNodeCombo->addItem(nodeName);
                if (nodeName == currentSelectedNode) {
                    selectedNodeItem = nodeItem;
                }
            }
        }
    }

    // Set the selected node index
    int idx = m_loadingNodeCombo->findText(currentSelectedNode);
    if (idx >= 0) {
        m_loadingNodeCombo->setCurrentIndex(idx);
    } else if (m_loadingNodeCombo->count() > 0) {
        m_loadingNodeCombo->setCurrentIndex(0);
        currentSelectedNode = m_loadingNodeCombo->currentText();
        // Find nodeItem again
        if (!projItems.isEmpty()) {
            QStandardItem* projItem = projItems.first();
            for (int i = 0; i < projItem->rowCount(); ++i) {
                QStandardItem* nodeItem = projItem->child(i, 0);
                if (nodeItem && nodeItem->text() == currentSelectedNode) {
                    selectedNodeItem = nodeItem;
                    break;
                }
            }
        }
    }

    m_loadingNodeName = currentSelectedNode;

    // Now populate the file list
    m_fileListWidget->clear();
    bool checkAll = m_checkedFilePaths.isEmpty() && m_isNewNode;
    if (selectedNodeItem) {
        for (int j = 0; j < selectedNodeItem->rowCount(); ++j) {
            QStandardItem* fileItem = selectedNodeItem->child(j, 0);
            QStandardItem* pathItem = selectedNodeItem->child(j, 1);
            if (fileItem && pathItem) {
                QListWidgetItem* listItem = new QListWidgetItem(fileItem->text(), m_fileListWidget);

                QString filePath = pathItem->text();
                listItem->setData(Qt::UserRole, filePath);

                if (m_checkedFilePaths.contains(filePath) || checkAll) {
                    listItem->setSelected(true);
                    if (!m_checkedFilePaths.contains(filePath)) {
                        m_checkedFilePaths.append(filePath);
                    }
                } else {
                    listItem->setSelected(false);
                }
            }
        }
    }

    m_isNewNode = false;

    m_loadingNodeCombo->blockSignals(false);
    m_fileListWidget->blockSignals(false);
}

void GeneralSARLoadingNode::refreshNodeList()
{
    refreshUI();
}

void GeneralSARLoadingNode::onProjectModelChanged(QStandardItemModel* model)
{
    if (model) {
        connect(model, &QAbstractItemModel::modelReset, this, &GeneralSARLoadingNode::refreshUI, Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::layoutChanged, this, &GeneralSARLoadingNode::refreshUI, Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::rowsInserted, this, &GeneralSARLoadingNode::refreshUI, Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &GeneralSARLoadingNode::refreshUI, Qt::UniqueConnection);
    }
    refreshUI();
}

void GeneralSARLoadingNode::onSelectionChanged()
{
    m_checkedFilePaths.clear();
    for (int i = 0; i < m_fileListWidget->count(); ++i) {
        QListWidgetItem* item = m_fileListWidget->item(i);
        if (item && item->isSelected()) {
            m_checkedFilePaths.append(item->data(Qt::UserRole).toString());
        }
    }
    setState(ExecutionState::Idle);
}

void GeneralSARLoadingNode::executeImport()
{
    // Verify file existence
    QStringList validPaths;
    for (const QString& path : m_checkedFilePaths) {
        if (QFileInfo::exists(path)) {
            validPaths.append(path);
        }
    }

    if (validPaths.isEmpty()) {
        onError(QStringLiteral("未选择任何有效文件。"));
        return;
    }

    m_selectedImageInfoData = std::make_shared<ImageInfoData>(validPaths);
    auto outputData = std::make_shared<ImportedFileData>(validPaths, m_loadingNodeName);
    setOutputData(0, outputData);
    setOutputData(1, m_selectedImageInfoData);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

    finishExecution();
}

QStringList GeneralSARLoadingNode::getImportedFilePaths() const
{
    return m_checkedFilePaths;
}

QString GeneralSARLoadingNode::getOutputNodeName() const
{
    return m_loadingNodeName;
}

QStringList GeneralSARLoadingNode::previewImagePaths() const
{
    QStringList existingPaths;
    for (const QString& path : m_checkedFilePaths) {
        if (QFileInfo::exists(path)) {
            existingPaths << path;
        }
    }
    return existingPaths;
}

QJsonObject GeneralSARLoadingNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["projectName"] = projectName();
    json["loadingNodeName"] = m_loadingNodeCombo ? m_loadingNodeCombo->currentText() : m_loadingNodeName;

    QJsonArray checkedPathsArray;
    for (const QString& path : m_checkedFilePaths) {
        checkedPathsArray.append(path);
    }
    json["checkedFilePaths"] = checkedPathsArray;
    return json;
}

void GeneralSARLoadingNode::load(QJsonObject const &json)
{
    m_loadingNodeName = json["loadingNodeName"].toString();

    QJsonArray checkedArray = json["checkedFilePaths"].toArray();
    m_checkedFilePaths.clear();
    for (const QJsonValue &val : checkedArray) {
        m_checkedFilePaths.append(val.toString());
    }

    m_isNewNode = false;

    // This calls validateAndRestoreOutput() which uses m_checkedFilePaths
    ImportNodeBase::load(json);

    // After calling base load, if the widget exists, we refresh it
    if (m_projectNameLabel) {
        refreshUI();
    }
}

unsigned int GeneralSARLoadingNode::nPorts(PortType portType) const
{
    if (portType == PortType::Out)
        return 2;
    return 0;
}

NodeDataType GeneralSARLoadingNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported Files"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool GeneralSARLoadingNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString GeneralSARLoadingNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool GeneralSARLoadingNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

std::shared_ptr<NodeData> GeneralSARLoadingNode::outData(PortIndex port)
{
    if (port == 0 || port == 1) {
        return m_selectedImageInfoData;
    }
    return nullptr;
}

void GeneralSARLoadingNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

bool GeneralSARLoadingNode::validateAndRestoreOutput()
{
    if (m_checkedFilePaths.isEmpty()) {
        return false;
    }

    // Check if the files still exist
    for (const QString& path : m_checkedFilePaths) {
        if (!QFileInfo::exists(path)) {
            return false;
        }
    }

    m_selectedImageInfoData = std::make_shared<ImageInfoData>(m_checkedFilePaths);
    auto importedData = std::make_shared<ImportedFileData>(m_checkedFilePaths, m_loadingNodeName);
    setOutputData(0, importedData);
    setOutputData(1, m_selectedImageInfoData);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    return true;
}

} // namespace QtNodes
