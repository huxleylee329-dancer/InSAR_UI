#include "LidarImportNode.h"
#include "LidarImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

LidarImportNode::LidarImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_productTypeCombo(nullptr)
    , m_rhPercentileSpin(nullptr)
    , m_rhLabel(nullptr)
    , m_filePaths()
    , m_outputNodeName("LiDAR_Import")
    , m_productType("GEDI L2A")
    , m_rhPercentile(100)
{
}

ProductOutputContract LidarImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("lidar_import.output.height_metric")
        : QStringLiteral("lidar_import.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("lidar_height_metric")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

QWidget* LidarImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);

    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Top section: file list
    auto* topSection = new QHBoxLayout();
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(80);
    topSection->addWidget(m_fileListWidget, 8);

    auto* buttonLayout = new QVBoxLayout();
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonLayout->addWidget(addFiles);
    buttonLayout->addWidget(removeFiles);
    topSection->addLayout(buttonLayout, 2);

    mainLayout->addLayout(topSection);

    // Configuration Options
    auto* configLayout = new QVBoxLayout();

    // Product Type Row
    auto* typeRow = new QHBoxLayout();
    QLabel* typeLabel = new QLabel("数据类型：");
    typeLabel->setFixedWidth(80);
    m_productTypeCombo = new QComboBox();
    m_productTypeCombo->addItems({"GEDI L2A", "GEDI L2B", "ICESat-2 L3A"});
    int prodIdx = m_productTypeCombo->findText(m_productType);
    if (prodIdx != -1) m_productTypeCombo->setCurrentIndex(prodIdx);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_productTypeCombo);
    configLayout->addLayout(typeRow);

    // RH Percentile Row
    auto* rhRow = new QHBoxLayout();
    m_rhLabel = new QLabel("RH百分位：");
    m_rhLabel->setFixedWidth(80);
    m_rhPercentileSpin = new QSpinBox();
    m_rhPercentileSpin->setRange(0, 100);
    m_rhPercentileSpin->setValue(m_rhPercentile);
    rhRow->addWidget(m_rhLabel);
    rhRow->addWidget(m_rhPercentileSpin);
    configLayout->addLayout(rhRow);

    // Target Node Row
    auto* nodeRow = new QHBoxLayout();
    QLabel* nodeLabel = new QLabel("目标节点：");
    nodeLabel->setFixedWidth(80);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "LiDAR_Import" : m_outputNodeName);
    nodeRow->addWidget(nodeLabel);
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    mainLayout->addLayout(configLayout);

    // Connect spinbox/combobox logic
    connect(m_productTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LidarImportNode::onProductTypeChanged);
    connect(m_rhPercentileSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, invalidateNodeData](int val) {
        if (m_rhPercentile != val) {
            m_rhPercentile = val;
            invalidateNodeData();
        }
    });

    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(getProjectContext(), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });

    // Connect buttons
    connect(addFiles, &QPushButton::clicked, this, &LidarImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &LidarImportNode::onRemoveFilesClicked);

    // Force setup UI state based on loaded indices
    onProductTypeChanged(m_productTypeCombo->currentIndex());

    return widget;
}

void LidarImportNode::onProductTypeChanged(int index)
{
    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    if (index == 0) {
        m_rhPercentileSpin->setEnabled(true);
        m_rhPercentileSpin->setValue(100);
        m_rhLabel->setEnabled(true);
    } else if (index == 1) {
        m_rhPercentileSpin->setEnabled(false);
        m_rhLabel->setEnabled(false);
    } else if (index == 2) {
        m_rhPercentileSpin->setEnabled(true);
        m_rhPercentileSpin->setValue(18);
        m_rhLabel->setEnabled(true);
    }

    QString newType = m_productTypeCombo->currentText();
    if (m_productType != newType) {
        m_productType = newType;
        invalidateNodeData();
    }
    m_rhPercentile = m_rhPercentileSpin->value();
}

void LidarImportNode::executeImport()
{
    if (m_filePaths.isEmpty())
    {
        onError("请至少添加一个 H5 文件。");
        return;
    }

    for (const QString& path : m_filePaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("H5 文件不存在：" + path);
            return;
        }
    }

    std::vector<ImportTask> tasks;
    for (const QString& path : m_filePaths)
    {
        QFileInfo fileInfo(path);
        ImportTask task;
        task.filename = fileInfo.baseName();
        task.arguments = QStringList{ path, m_productTypeCombo->currentText(), QString::number(m_rhPercentileSpin->value()) };
        tasks.push_back(task);
    }

    startWorker(new LidarImportWorker(), tasks);
}

QStringList LidarImportNode::getExpectedOutputFilePaths() const
{
    QStringList expectedPaths;
    QString outputNodeName = getOutputNodeName();
    for (const QString& path : m_filePaths)
    {
        QString importName = QFileInfo(path).baseName();
        expectedPaths.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
    }
    return expectedPaths;
}

QString LidarImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit) {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (!name.isEmpty())
            return name;
    }
    return m_outputNodeName.isEmpty() ? "LiDAR_Import" : m_outputNodeName;
}

void LidarImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("导入 LiDAR 数据"),
        QDir::currentPath(),
        tr("HDF5 文件 (*.h5)")
    );

    for (const QString& file : files)
    {
        if (!m_filePaths.contains(file))
        {
            m_filePaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());

            int outCount = nPorts(PortType::Out);
            for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
            invalidateExecution();
        }
    }
    updateWidgetSize();
}

void LidarImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_filePaths.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
    updateWidgetSize();
}

QJsonObject LidarImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_filePaths)
        pathsArray.append(path);
    json["filePaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    json["productType"] = m_productTypeCombo ? m_productTypeCombo->currentText() : m_productType;
    json["rhPercentile"] = m_rhPercentileSpin ? m_rhPercentileSpin->value() : m_rhPercentile;
    return json;
}

void LidarImportNode::load(QJsonObject const &json)
{
    m_filePaths.clear();
    QJsonArray pathsArray = json["filePaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_filePaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("LiDAR_Import");
    m_productType = json["productType"].toString("GEDI L2A");
    m_rhPercentile = json["rhPercentile"].toInt(100);

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_filePaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_productTypeCombo) {
        int idx = m_productTypeCombo->findText(m_productType);
        if (idx != -1) {
            m_productTypeCombo->setCurrentIndex(idx);
        }
    }

    if (m_rhPercentileSpin) {
        m_rhPercentileSpin->setValue(m_rhPercentile);
    }
}

void LidarImportNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

} // namespace QtNodes
