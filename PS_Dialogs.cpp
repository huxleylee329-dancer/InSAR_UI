#include "PS_Dialogs.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "tinyxml.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

// ============================================================================
// 1. PS_Candidate_Dialog 实现
// ============================================================================

PS_Candidate_Dialog::PS_Candidate_Dialog(QWidget* parent)
    : QDialog(parent)
    , m_projectModel(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setWindowTitle(QStringLiteral("PS 候选点选择 (PSI Step 1)"));
    resize(380, 280);
    createUI();
}

PS_Candidate_Dialog::~PS_Candidate_Dialog()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS candidate dialog destroyed"));
}

void PS_Candidate_Dialog::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_projectCombo = new QComboBox(this);
    m_srcNodeCombo = new QComboBox(this);
    m_daThresholdEdit = new QLineEdit("0.4", this);
    m_minPsCountEdit = new QLineEdit("1000", this);
    m_multilookRgEdit = new QLineEdit("1", this);
    m_multilookAzEdit = new QLineEdit("1", this);
    m_outputNodeNameEdit = new QLineEdit("PS_Candidates", this);

    formLayout->addRow(QStringLiteral("选择工程:"), m_projectCombo);
    formLayout->addRow(QStringLiteral("配准后影像集:"), m_srcNodeCombo);
    formLayout->addRow(QStringLiteral("振幅离差阈值:"), m_daThresholdEdit);
    formLayout->addRow(QStringLiteral("最小PS点数:"), m_minPsCountEdit);
    formLayout->addRow(QStringLiteral("距离向多视:"), m_multilookRgEdit);
    formLayout->addRow(QStringLiteral("方位向多视:"), m_multilookAzEdit);
    formLayout->addRow(QStringLiteral("目标节点名:"), m_outputNodeNameEdit);

    mainLayout->addLayout(formLayout);

    m_statusLabel = new QLabel(QStringLiteral("状态：准备就绪"), this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PS_Candidate_Dialog::onProjectChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PS_Candidate_Dialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PS_Candidate_Dialog::onReject);
}

void PS_Candidate_Dialog::ShowProjectList(QStandardItemModel* model)
{
    m_projectModel = model;
    m_projectCombo->clear();
    m_srcNodeCombo->clear();
    m_projectPath.clear();
    m_projectName.clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2) return;

    for (int i = 0; i < model->rowCount(); ++i) {
        QStandardItem* projectItem = model->item(i, 0);
        QStandardItem* pathItem = model->item(i, 1);
        if (!projectItem || !pathItem) continue;
        m_projectCombo->addItem(projectItem->text());
    }
    if (m_projectCombo->count() > 0) {
        onProjectChanged();
    }
}

void PS_Candidate_Dialog::onProjectChanged()
{
    m_srcNodeCombo->clear();
    m_projectPath.clear();
    m_projectName.clear();
    if (!m_projectModel || m_projectCombo->count() == 0) return;

    QString projName = m_projectCombo->currentText();
    QStandardItem* projectItem = NodeUtils::findFirstModelItem(m_projectModel, projName);
    if (!projectItem) return;
    QStandardItem* pathItem = m_projectModel->item(projectItem->row(), 1);
    if (!pathItem) return;

    m_projectPath = pathItem->text();
    m_projectName = projName;

    // 筛选 complex-2.0 节点（配准后SLC映像集）
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        QStandardItem* rankItem = projectItem->child(i, 1);
        if (childItem && rankItem && rankItem->text() == "complex-2.0") {
            m_srcNodeCombo->addItem(childItem->text());
        }
    }
}

void PS_Candidate_Dialog::onAccept()
{
    if (m_thread && m_thread->isRunning()) return;

    QString selectedNode = m_srcNodeCombo->currentText();
    if (selectedNode.isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请先选择配准后的影像集！"));
        return;
    }

    if (!m_projectModel) {
        QMessageBox::warning(this, "Warning", QStringLiteral("工程数据不可用，请重新打开工程。"));
        return;
    }
    QString projName = m_projectCombo->currentText();
    QStandardItem* projectItem = NodeUtils::findFirstModelItem(m_projectModel, projName);
    if (!projectItem) {
        QMessageBox::warning(this, "Warning", QStringLiteral("所选工程不存在或已被关闭。"));
        return;
    }

    // 找到该节点的子文件路径列表
    QStandardItem* srcNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        if (childItem && childItem->text() == selectedNode) {
            srcNodeItem = childItem;
            break;
        }
    }

    if (!srcNodeItem || srcNodeItem->rowCount() == 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("所选影像集为空！"));
        return;
    }

    QStringList slcList;
    for (int i = 0; i < srcNodeItem->rowCount(); ++i) {
        QStandardItem* pathItem = srcNodeItem->child(i, 1);
        if (pathItem && !pathItem->text().isEmpty()) {
            slcList.append(pathItem->text());
        }
    }
    if (slcList.isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("所选影像集没有有效的数据路径！"));
        return;
    }

    double daThresh = m_daThresholdEdit->text().toDouble();
    int minPs = m_minPsCountEdit->text().toInt();
    int rg = m_multilookRgEdit->text().toInt();
    int az = m_multilookAzEdit->text().toInt();
    QString outName = m_outputNodeNameEdit->text();

    if (outName.isEmpty() || daThresh <= 0.0 || minPs <= 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请输入合法的参数！"));
        return;
    }

    const QString root = projectRoot();
    if (root.isEmpty() || !QFileInfo(projectXmlPath()).isFile()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("工程 XML 文件不可用！"));
        return;
    }
    m_preparedOutputNode = outName;
    m_preparedOutputPaths = QStringList() <<
        QDir(root).absoluteFilePath(outName + "/PS_candidates.h5");
    m_preparedInputPaths = slcList;
    m_generatedOutputPaths.clear();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(root, m_preparedOutputNode, m_preparedOutputPaths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError, nullptr,
                                           projectXmlPath())) {
        QMessageBox::warning(this, "Warning", transactionError);
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在初始化后台线程..."));
    m_progressBar->setValue(0);

    m_thread = new QThread(this);
    m_worker = new PSCandidateWorker();
    m_worker->moveToThread(m_thread);
    const QString stagingNode = m_outputTransaction.stagingName;

    connect(m_thread, &QThread::started, m_worker, [this, daThresh, minPs, rg, az, root, stagingNode]() {
        m_worker->select_candidates(daThresh, minPs, rg, az, root, m_projectName, stagingNode,
                                    m_preparedInputPaths, true);
    });

    connect(m_worker, &PSCandidateWorker::updateProcess, this, &PS_Candidate_Dialog::onProgressUpdate);
    connect(m_worker, &PSCandidateWorker::outputsGenerated, this, &PS_Candidate_Dialog::onOutputsGenerated);
    connect(m_worker, &PSCandidateWorker::endProcess, this, &PS_Candidate_Dialog::onProcessingFinished);
    connect(m_worker, &PSCandidateWorker::errorProcess, this, &PS_Candidate_Dialog::onError);
    connect(m_worker, &PSCandidateWorker::cancelled, this, &PS_Candidate_Dialog::onCancelled);
    connect(m_worker, &PSCandidateWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSCandidateWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSCandidateWorker::cancelled, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void PS_Candidate_Dialog::onReject()
{
    reject();
}

void PS_Candidate_Dialog::stopThread()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

QString PS_Candidate_Dialog::projectRoot() const
{
    return m_projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(m_projectPath).absolutePath() : m_projectPath;
}

QString PS_Candidate_Dialog::projectXmlPath() const
{
    if (m_projectPath.endsWith(".insar", Qt::CaseInsensitive)) {
        return m_projectPath;
    }
    return QDir(projectRoot()).absoluteFilePath(m_projectName);
}

void PS_Candidate_Dialog::reject()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS candidate dialog cancelled"));
    QDialog::reject();
}

void PS_Candidate_Dialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("进度 (%1%): %2").arg(progress).arg(message));
}

void PS_Candidate_Dialog::onOutputsGenerated(const QStringList& outputPaths)
{
    m_generatedOutputPaths = outputPaths;
}

void PS_Candidate_Dialog::onError(const QString& error)
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error);
    m_statusLabel->setText(QStringLiteral("状态：出错中断"));
    QMessageBox::critical(this, "Error", QStringLiteral("计算出错: ") + error);
}

void PS_Candidate_Dialog::onCancelled()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS candidate calculation cancelled"));
    m_statusLabel->setText(QStringLiteral("状态：已取消"));
}

void PS_Candidate_Dialog::onProcessingFinished()
{
    const QList<QStandardItem*> projects = m_projectModel
        ? m_projectModel->findItems(m_projectName) : QList<QStandardItem*>();
    QString transactionError;
    QStringList finalPaths;
    XMLFile xml;
    const QString xmlPath = projectXmlPath();
    if (projects.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("amplitude_dispersion") << QStringLiteral("ps_mask"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0 ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, &xml, xmlPath, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("PS 候选点输出事务校验失败。") : transactionError;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        m_statusLabel->setText(QStringLiteral("状态：出错中断"));
        QMessageBox::critical(this, "Error", reason);
        return;
    }

    TiXmlElement* root = nullptr;
    if (xml.get_root(root) < 0 || !root) {
        const QString reason = QStringLiteral("无法读取工程 XML 根节点。");
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        QMessageBox::critical(this, "Error", reason);
        return;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_preparedOutputNode == QString::fromLocal8Bit(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    const QString h5Path = finalPaths.value(0);
    const QString relativePath = QString("/%1/%2").arg(m_preparedOutputNode, QFileInfo(h5Path).fileName());
    xml.XMLFile_add_unwrap(m_preparedOutputNode.toStdString().c_str(), "PS_candidates",
                            relativePath.toStdString().c_str(), 0, 0, "PS_Candidates", 0);
    if (!NodeUtils::saveProjectXmlAtomically(&xml, xmlPath, &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("PS 候选点元数据提交失败。") : transactionError;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        QMessageBox::critical(this, "Error", reason);
        return;
    }

    QStandardItem* projectItem = projects.first();
    for (int row = projectItem->rowCount() - 1; row >= 0; --row) {
        QStandardItem* nodeItem = projectItem->child(row, 0);
        if (nodeItem && nodeItem->text() == m_preparedOutputNode) {
            projectItem->removeRow(row);
        }
    }
    QStandardItem* parentNode = NodeUtils::findOrCreateProjectNode(projectItem, m_preparedOutputNode, "mask-1.0");
    NodeUtils::findOrCreateChildItem(parentNode, "PS_candidates.h5", "mask-1.0", h5Path);
    emit sendCopy(m_projectModel);

    m_statusLabel->setText(QStringLiteral("计算成功完成！已注册节点 %1").arg(m_preparedOutputNode));
    m_progressBar->setValue(100);
    QMessageBox::information(this, "Success", QStringLiteral("PS 候选点提取完成！"));
    stopThread();
    accept();
}


// ============================================================================
// 2. PS_Network_Dialog 实现
// ============================================================================

PS_Network_Dialog::PS_Network_Dialog(QWidget* parent)
    : QDialog(parent)
    , m_projectModel(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setWindowTitle(QStringLiteral("PS 网络构建 (PSI Step 2)"));
    resize(380, 280);
    createUI();
}

PS_Network_Dialog::~PS_Network_Dialog()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS network dialog destroyed"));
}

void PS_Network_Dialog::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_projectCombo = new QComboBox(this);
    m_candidatesCombo = new QComboBox(this);
    m_slcCombo = new QComboBox(this);
    m_maxEdgeLengthEdit = new QLineEdit("1000.0", this);
    m_refRowEdit = new QLineEdit("-1", this);
    m_refColEdit = new QLineEdit("-1", this);
    m_outputNodeNameEdit = new QLineEdit("PS_Network", this);

    formLayout->addRow(QStringLiteral("选择工程:"), m_projectCombo);
    formLayout->addRow(QStringLiteral("PS候选点节点:"), m_candidatesCombo);
    formLayout->addRow(QStringLiteral("配准后影像集:"), m_slcCombo);
    formLayout->addRow(QStringLiteral("最大连接距离:"), m_maxEdgeLengthEdit);
    formLayout->addRow(QStringLiteral("参考点行:"), m_refRowEdit);
    formLayout->addRow(QStringLiteral("参考点列:"), m_refColEdit);
    formLayout->addRow(QStringLiteral("目标节点名:"), m_outputNodeNameEdit);

    mainLayout->addLayout(formLayout);

    m_statusLabel = new QLabel(QStringLiteral("状态：准备就绪"), this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PS_Network_Dialog::onProjectChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PS_Network_Dialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PS_Network_Dialog::onReject);
}

void PS_Network_Dialog::ShowProjectList(QStandardItemModel* model)
{
    m_projectModel = model;
    m_projectCombo->clear();
    m_candidatesCombo->clear();
    m_slcCombo->clear();
    m_projectPath.clear();
    m_projectName.clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2) return;

    for (int i = 0; i < model->rowCount(); ++i) {
        QStandardItem* projectItem = model->item(i, 0);
        QStandardItem* pathItem = model->item(i, 1);
        if (!projectItem || !pathItem) continue;
        m_projectCombo->addItem(projectItem->text());
    }
    if (m_projectCombo->count() > 0) {
        onProjectChanged();
    }
}

void PS_Network_Dialog::onProjectChanged()
{
    m_candidatesCombo->clear();
    m_slcCombo->clear();
    m_projectPath.clear();
    m_projectName.clear();
    if (!m_projectModel || m_projectCombo->count() == 0) return;

    
    QString projName = m_projectCombo->currentText();
    QStandardItem* projectItem = NodeUtils::findFirstModelItem(m_projectModel, projName);
    if (!projectItem) return;
    QStandardItem* pathItem = m_projectModel->item(projectItem->row(), 1);
    if (!pathItem) return;

    m_projectPath = pathItem->text();
    m_projectName = projName;

    // 筛选子节点
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        QStandardItem* rankItem = projectItem->child(i, 1);
        if (!childItem || !rankItem) continue;
        const QString rtype = rankItem->text();
        const QString name = childItem->text();
        if (rtype == "mask-1.0") {
            m_candidatesCombo->addItem(name);
        } else if (rtype == "complex-2.0") {
            m_slcCombo->addItem(name);
        }
    }
}

void PS_Network_Dialog::onAccept()
{
    if (m_thread && m_thread->isRunning()) return;

    QString candidatesNode = m_candidatesCombo->currentText();
    QString slcNode = m_slcCombo->currentText();
    if (candidatesNode.isEmpty() || slcNode.isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请确保已导入候选点且选择了影像集！"));
        return;
    }

    if (!m_projectModel) {
        QMessageBox::warning(this, "Warning", QStringLiteral("工程数据不可用，请重新打开工程。"));
        return;
    }
    QString projName = m_projectCombo->currentText();
    QStandardItem* projectItem = NodeUtils::findFirstModelItem(m_projectModel, projName);
    if (!projectItem) {
        QMessageBox::warning(this, "Warning", QStringLiteral("所选工程不存在或已被关闭。"));
        return;
    }

    // 1. 查找候选点文件路径 (PS_candidates.h5)
    QStandardItem* candNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        if (childItem && childItem->text() == candidatesNode) {
            candNodeItem = childItem;
            break;
        }
    }
    if (!candNodeItem || candNodeItem->rowCount() == 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("候选点数据文件不存在！"));
        return;
    }
    QStandardItem* candidatePathItem = candNodeItem->child(0, 1);
    if (!candidatePathItem || candidatePathItem->text().isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("候选点数据路径不存在！"));
        return;
    }
    QString candH5Path = candidatePathItem->text();

    // 2. 查找配准影像集路径列表
    QStandardItem* slcNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        if (childItem && childItem->text() == slcNode) {
            slcNodeItem = childItem;
            break;
        }
    }
    if (!slcNodeItem || slcNodeItem->rowCount() == 0) return;
    QStringList slcList;
    for (int i = 0; i < slcNodeItem->rowCount(); ++i) {
        QStandardItem* pathItem = slcNodeItem->child(i, 1);
        if (pathItem && !pathItem->text().isEmpty()) {
            slcList.append(pathItem->text());
        }
    }
    if (slcList.size() < 2) {
        QMessageBox::warning(this, "Warning", QStringLiteral("PS 网络至少需要两景配准影像！"));
        return;
    }

    double maxEdge = m_maxEdgeLengthEdit->text().toDouble();
    int refRow = m_refRowEdit->text().toInt();
    int refCol = m_refColEdit->text().toInt();
    QString outName = m_outputNodeNameEdit->text();

    if (outName.isEmpty() || maxEdge <= 0.0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请输入合法参数！"));
        return;
    }

    const QString root = projectRoot();
    if (root.isEmpty() || !QFileInfo(projectXmlPath()).isFile()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("工程 XML 文件不可用！"));
        return;
    }
    m_preparedOutputNode = outName;
    m_preparedOutputPaths = QStringList() <<
        QDir(root).absoluteFilePath(outName + "/PS_network.h5");
    m_preparedInputPaths = QStringList() << candH5Path;
    m_preparedInputPaths.append(slcList);
    m_generatedOutputPaths.clear();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(root, m_preparedOutputNode, m_preparedOutputPaths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError, nullptr,
                                           projectXmlPath())) {
        QMessageBox::warning(this, "Warning", transactionError);
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在初始化网络构建线程..."));
    m_progressBar->setValue(0);

    m_thread = new QThread(this);
    m_worker = new PSNetworkWorker();
    m_worker->moveToThread(m_thread);
    const QString stagingNode = m_outputTransaction.stagingName;

    connect(m_thread, &QThread::started, m_worker, [this, maxEdge, refRow, refCol, root, stagingNode]() {
        const QString candidatesH5 = m_preparedInputPaths.value(0);
        QStringList slcPaths = m_preparedInputPaths;
        if (!slcPaths.isEmpty()) {
            slcPaths.removeFirst();
        }
        m_worker->build_network(maxEdge, refRow, refCol, root, m_projectName, stagingNode,
                                candidatesH5, slcPaths, true);
    });

    connect(m_worker, &PSNetworkWorker::updateProcess, this, &PS_Network_Dialog::onProgressUpdate);
    connect(m_worker, &PSNetworkWorker::outputsGenerated, this, &PS_Network_Dialog::onOutputsGenerated);
    connect(m_worker, &PSNetworkWorker::endProcess, this, &PS_Network_Dialog::onProcessingFinished);
    connect(m_worker, &PSNetworkWorker::errorProcess, this, &PS_Network_Dialog::onError);
    connect(m_worker, &PSNetworkWorker::cancelled, this, &PS_Network_Dialog::onCancelled);
    connect(m_worker, &PSNetworkWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSNetworkWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSNetworkWorker::cancelled, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void PS_Network_Dialog::onReject()
{
    reject();
}

void PS_Network_Dialog::stopThread()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

QString PS_Network_Dialog::projectRoot() const
{
    return m_projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(m_projectPath).absolutePath() : m_projectPath;
}

QString PS_Network_Dialog::projectXmlPath() const
{
    if (m_projectPath.endsWith(".insar", Qt::CaseInsensitive)) {
        return m_projectPath;
    }
    return QDir(projectRoot()).absoluteFilePath(m_projectName);
}

void PS_Network_Dialog::reject()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS network dialog cancelled"));
    QDialog::reject();
}

void PS_Network_Dialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("进度 (%1%): %2").arg(progress).arg(message));
}

void PS_Network_Dialog::onOutputsGenerated(const QStringList& outputPaths)
{
    m_generatedOutputPaths = outputPaths;
}

void PS_Network_Dialog::onError(const QString& error)
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error);
    m_statusLabel->setText(QStringLiteral("状态：出错中断"));
    QMessageBox::critical(this, "Error", QStringLiteral("计算出错: ") + error);
}

void PS_Network_Dialog::onCancelled()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS network calculation cancelled"));
    m_statusLabel->setText(QStringLiteral("状态：已取消"));
}

void PS_Network_Dialog::onProcessingFinished()
{
    const QList<QStandardItem*> projects = m_projectModel
        ? m_projectModel->findItems(m_projectName) : QList<QStandardItem*>();
    QString transactionError;
    QStringList finalPaths;
    XMLFile xml;
    const QString xmlPath = projectXmlPath();
    if (projects.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("ps_coordinates") << QStringLiteral("edges") <<
                QStringLiteral("edge_phase_diff") << QStringLiteral("temporal_baseline") <<
                QStringLiteral("spatial_baseline") << QStringLiteral("formation_matrix"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0 ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, &xml, xmlPath, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("PS 网络输出事务校验失败。") : transactionError;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        m_statusLabel->setText(QStringLiteral("状态：出错中断"));
        QMessageBox::critical(this, "Error", reason);
        return;
    }

    TiXmlElement* root = nullptr;
    if (xml.get_root(root) < 0 || !root) {
        const QString reason = QStringLiteral("无法读取工程 XML 根节点。");
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        QMessageBox::critical(this, "Error", reason);
        return;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_preparedOutputNode == QString::fromLocal8Bit(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    const QString h5Path = finalPaths.value(0);
    const QString relativePath = QString("/%1/%2").arg(m_preparedOutputNode, QFileInfo(h5Path).fileName());
    xml.XMLFile_add_unwrap(m_preparedOutputNode.toStdString().c_str(), "PS_network",
                            relativePath.toStdString().c_str(), 0, 0, "PS_Network", 0);
    if (!NodeUtils::saveProjectXmlAtomically(&xml, xmlPath, &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("PS 网络元数据提交失败。") : transactionError;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        QMessageBox::critical(this, "Error", reason);
        return;
    }

    QStandardItem* projectItem = projects.first();
    for (int row = projectItem->rowCount() - 1; row >= 0; --row) {
        QStandardItem* nodeItem = projectItem->child(row, 0);
        if (nodeItem && nodeItem->text() == m_preparedOutputNode) {
            projectItem->removeRow(row);
        }
    }
    QStandardItem* parentNode = NodeUtils::findOrCreateProjectNode(projectItem, m_preparedOutputNode, "mask-1.0");
    NodeUtils::findOrCreateChildItem(parentNode, "PS_network.h5", "mask-1.0", h5Path);
    emit sendCopy(m_projectModel);

    m_statusLabel->setText(QStringLiteral("计算成功完成！已注册网络节点 %1").arg(m_preparedOutputNode));
    m_progressBar->setValue(100);
    QMessageBox::information(this, "Success", QStringLiteral("PS Delaunay 网络构建完成！"));
    stopThread();
    accept();
}


// ============================================================================
// 3. PS_TimeSeries_Dialog 实现
// ============================================================================

PS_TimeSeries_Dialog::PS_TimeSeries_Dialog(QWidget* parent)
    : QDialog(parent)
    , m_projectModel(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setWindowTitle(QStringLiteral("PS 时序反演分析 (PSI Step 3)"));
    resize(380, 240);
    createUI();
}

PS_TimeSeries_Dialog::~PS_TimeSeries_Dialog()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS time-series dialog destroyed"));
}

void PS_TimeSeries_Dialog::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_projectCombo = new QComboBox(this);
    m_networkCombo = new QComboBox(this);
    m_coherenceThreshEdit = new QLineEdit("0.7", this);
    m_maxDeformationRateEdit = new QLineEdit("0.1", this);
    m_atmosphericWindowEdit = new QLineEdit("500", this);
    m_outputNodeNameEdit = new QLineEdit("PS_TimeSeries", this);

    formLayout->addRow(QStringLiteral("选择工程:"), m_projectCombo);
    formLayout->addRow(QStringLiteral("PS网格网络节点:"), m_networkCombo);
    formLayout->addRow(QStringLiteral("时间相干性阈值:"), m_coherenceThreshEdit);
    formLayout->addRow(QStringLiteral("最大形变速率:"), m_maxDeformationRateEdit);
    formLayout->addRow(QStringLiteral("大气滤波窗口(米):"), m_atmosphericWindowEdit);
    formLayout->addRow(QStringLiteral("目标节点名:"), m_outputNodeNameEdit);

    mainLayout->addLayout(formLayout);

    m_statusLabel = new QLabel(QStringLiteral("状态：准备就绪"), this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PS_TimeSeries_Dialog::onProjectChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PS_TimeSeries_Dialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PS_TimeSeries_Dialog::onReject);
}

void PS_TimeSeries_Dialog::ShowProjectList(QStandardItemModel* model)
{
    m_projectModel = model;
    m_projectCombo->clear();
    m_networkCombo->clear();
    m_projectPath.clear();
    m_projectName.clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2) return;

    for (int i = 0; i < model->rowCount(); ++i) {
        QStandardItem* projectItem = model->item(i, 0);
        QStandardItem* pathItem = model->item(i, 1);
        if (!projectItem || !pathItem) continue;
        m_projectCombo->addItem(projectItem->text());
    }
    if (m_projectCombo->count() > 0) {
        onProjectChanged();
    }
}

void PS_TimeSeries_Dialog::onProjectChanged()
{
    m_networkCombo->clear();
    m_projectPath.clear();
    m_projectName.clear();
    if (!m_projectModel || m_projectCombo->count() == 0) return;

    
    QString projName = m_projectCombo->currentText();
    QStandardItem* projectItem = NodeUtils::findFirstModelItem(m_projectModel, projName);
    if (!projectItem) return;
    QStandardItem* pathItem = m_projectModel->item(projectItem->row(), 1);
    if (!pathItem) return;

    m_projectPath = pathItem->text();
    m_projectName = projName;

    // 筛选子节点 (主要是 mask-1.0，PSNetworkWorker 也是输出的 mask-1.0 类型的网络成果)
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        QStandardItem* rankItem = projectItem->child(i, 1);
        if (!childItem || !rankItem) continue;
        const QString rtype = rankItem->text();
        const QString name = childItem->text();
        if (rtype == "mask-1.0" && name.contains("Network", Qt::CaseInsensitive)) {
            m_networkCombo->addItem(name);
        }
    }
}

void PS_TimeSeries_Dialog::onAccept()
{
    if (m_thread && m_thread->isRunning()) return;

    QString networkNodeName = m_networkCombo->currentText();
    if (networkNodeName.isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请先选择 PS 网络成果节点！"));
        return;
    }

    if (!m_projectModel) {
        QMessageBox::warning(this, "Warning", QStringLiteral("工程数据不可用，请重新打开工程。"));
        return;
    }
    QString projName = m_projectCombo->currentText();
    QStandardItem* projectItem = NodeUtils::findFirstModelItem(m_projectModel, projName);
    if (!projectItem) {
        QMessageBox::warning(this, "Warning", QStringLiteral("所选工程不存在或已被关闭。"));
        return;
    }

    // 查找网络 H5 路径
    QStandardItem* netNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* childItem = projectItem->child(i, 0);
        if (childItem && childItem->text() == networkNodeName) {
            netNodeItem = childItem;
            break;
        }
    }
    if (!netNodeItem || netNodeItem->rowCount() == 0) return;
    QStandardItem* networkPathItem = netNodeItem->child(0, 1);
    if (!networkPathItem || networkPathItem->text().isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("PS 网络数据路径不存在！"));
        return;
    }
    QString netH5Path = networkPathItem->text();

    double cohThresh = m_coherenceThreshEdit->text().toDouble();
    double maxDef = m_maxDeformationRateEdit->text().toDouble();
    int atmosWin = m_atmosphericWindowEdit->text().toInt();
    QString outName = m_outputNodeNameEdit->text();

    if (outName.isEmpty() || cohThresh <= 0.0 || maxDef <= 0.0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请输入合法参数！"));
        return;
    }

    const QString root = projectRoot();
    if (root.isEmpty() || !QFileInfo(projectXmlPath()).isFile()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("工程 XML 文件不可用！"));
        return;
    }
    m_preparedOutputNode = outName;
    m_preparedOutputPaths = QStringList() <<
        QDir(root).absoluteFilePath(outName + "/PS_time_series.h5");
    m_preparedInputPaths = QStringList() << netH5Path;
    m_generatedOutputPaths.clear();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(root, m_preparedOutputNode, m_preparedOutputPaths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError, nullptr,
                                           projectXmlPath())) {
        QMessageBox::warning(this, "Warning", transactionError);
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在初始化时序反演分析线程..."));
    m_progressBar->setValue(0);

    m_thread = new QThread(this);
    m_worker = new PSTimeSeriesWorker();
    m_worker->moveToThread(m_thread);
    const QString stagingNode = m_outputTransaction.stagingName;

    connect(m_thread, &QThread::started, m_worker, [this, cohThresh, maxDef, atmosWin, root, stagingNode]() {
        m_worker->ps_time_series(cohThresh, maxDef, atmosWin, root, m_projectName, stagingNode,
                                 m_preparedInputPaths, true);
    });

    connect(m_worker, &PSTimeSeriesWorker::updateProcess, this, &PS_TimeSeries_Dialog::onProgressUpdate);
    connect(m_worker, &PSTimeSeriesWorker::outputsGenerated, this, &PS_TimeSeries_Dialog::onOutputsGenerated);
    connect(m_worker, &PSTimeSeriesWorker::endProcess, this, &PS_TimeSeries_Dialog::onProcessingFinished);
    connect(m_worker, &PSTimeSeriesWorker::errorProcess, this, &PS_TimeSeries_Dialog::onError);
    connect(m_worker, &PSTimeSeriesWorker::cancelled, this, &PS_TimeSeries_Dialog::onCancelled);
    connect(m_worker, &PSTimeSeriesWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSTimeSeriesWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSTimeSeriesWorker::cancelled, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void PS_TimeSeries_Dialog::onReject()
{
    reject();
}

void PS_TimeSeries_Dialog::stopThread()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

QString PS_TimeSeries_Dialog::projectRoot() const
{
    return m_projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(m_projectPath).absolutePath() : m_projectPath;
}

QString PS_TimeSeries_Dialog::projectXmlPath() const
{
    if (m_projectPath.endsWith(".insar", Qt::CaseInsensitive)) {
        return m_projectPath;
    }
    return QDir(projectRoot()).absoluteFilePath(m_projectName);
}

void PS_TimeSeries_Dialog::reject()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS time-series dialog cancelled"));
    QDialog::reject();
}

void PS_TimeSeries_Dialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("进度 (%1%): %2").arg(progress).arg(message));
}

void PS_TimeSeries_Dialog::onOutputsGenerated(const QStringList& outputPaths)
{
    m_generatedOutputPaths = outputPaths;
}

void PS_TimeSeries_Dialog::onError(const QString& error)
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error);
    m_statusLabel->setText(QStringLiteral("状态：出错中断"));
    QMessageBox::critical(this, "Error", QStringLiteral("计算出错: ") + error);
}

void PS_TimeSeries_Dialog::onCancelled()
{
    stopThread();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("PS time-series calculation cancelled"));
    m_statusLabel->setText(QStringLiteral("状态：已取消"));
}

void PS_TimeSeries_Dialog::onProcessingFinished()
{
    const QList<QStandardItem*> projects = m_projectModel
        ? m_projectModel->findItems(m_projectName) : QList<QStandardItem*>();
    QString transactionError;
    QStringList finalPaths;
    XMLFile xml;
    const QString xmlPath = projectXmlPath();
    if (projects.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("ps_coordinates") << QStringLiteral("deformation_velocity") <<
                QStringLiteral("temporal_coherence") << QStringLiteral("topographic_residual") <<
                QStringLiteral("deformation_time_series") << QStringLiteral("mask") <<
                QStringLiteral("mask_count_map") << QStringLiteral("temporal_baseline"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0 ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, &xml, xmlPath, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("PS 时序输出事务校验失败。") : transactionError;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        m_statusLabel->setText(QStringLiteral("状态：出错中断"));
        QMessageBox::critical(this, "Error", reason);
        return;
    }

    TiXmlElement* root = nullptr;
    if (xml.get_root(root) < 0 || !root) {
        const QString reason = QStringLiteral("无法读取工程 XML 根节点。");
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        QMessageBox::critical(this, "Error", reason);
        return;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_preparedOutputNode == QString::fromLocal8Bit(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    const QString h5Path = finalPaths.value(0);
    const QString relativePath = QString("/%1/%2").arg(m_preparedOutputNode, QFileInfo(h5Path).fileName());
    xml.XMLFile_add_unwrap(m_preparedOutputNode.toStdString().c_str(), "PS_time_series",
                            relativePath.toStdString().c_str(), 0, 0, "PS_TimeSeries", 0);
    if (!NodeUtils::saveProjectXmlAtomically(&xml, xmlPath, &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("PS 时序元数据提交失败。") : transactionError;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        stopThread();
        QMessageBox::critical(this, "Error", reason);
        return;
    }

    QStandardItem* projectItem = projects.first();
    for (int row = projectItem->rowCount() - 1; row >= 0; --row) {
        QStandardItem* nodeItem = projectItem->child(row, 0);
        if (nodeItem && nodeItem->text() == m_preparedOutputNode) {
            projectItem->removeRow(row);
        }
    }
    QStandardItem* parentNode = NodeUtils::findOrCreateProjectNode(projectItem, m_preparedOutputNode, "double-1.0");
    NodeUtils::findOrCreateChildItem(parentNode, "PS_time_series.h5", "double-1.0", h5Path);
    emit sendCopy(m_projectModel);

    m_statusLabel->setText(QStringLiteral("计算成功完成！已注册时序成果节点 %1").arg(m_preparedOutputNode));
    m_progressBar->setValue(100);
    QMessageBox::information(this, "Success", QStringLiteral("PS 时序反演完成！"));
    stopThread();
    accept();
}

} // namespace QtNodes
