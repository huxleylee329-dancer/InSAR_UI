#include "PS_Dialogs.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InSARLogManager.h"
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
    for (int i = 0; i < model->rowCount(); ++i) {
        m_projectCombo->addItem(model->item(i, 0)->text());
    }
    if (model->rowCount() > 0) {
        onProjectChanged();
    }
}

void PS_Candidate_Dialog::onProjectChanged()
{
    if (!m_projectModel || m_projectCombo->count() == 0) return;

    m_srcNodeCombo->clear();
    QString projName = m_projectCombo->currentText();
    QList<QStandardItem*> found = m_projectModel->findItems(projName);
    if (found.isEmpty()) return;

    QStandardItem* projectItem = found.first();
    m_projectPath = m_projectModel->item(projectItem->row(), 1)->text();
    m_projectName = projName;

    // 筛选 complex-2.0 节点（配准后SLC映像集）
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 1)->text() == "complex-2.0") {
            m_srcNodeCombo->addItem(projectItem->child(i, 0)->text());
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

    QString projName = m_projectCombo->currentText();
    QList<QStandardItem*> found = m_projectModel->findItems(projName);
    if (found.isEmpty()) return;
    QStandardItem* projectItem = found.first();

    // 找到该节点的子文件路径列表
    QStandardItem* srcNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 0)->text() == selectedNode) {
            srcNodeItem = projectItem->child(i, 0);
            break;
        }
    }

    if (!srcNodeItem || srcNodeItem->rowCount() == 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("所选影像集为空！"));
        return;
    }

    QStringList slcList;
    for (int i = 0; i < srcNodeItem->rowCount(); ++i) {
        slcList.append(srcNodeItem->child(i, 1)->text());
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

    m_statusLabel->setText(QStringLiteral("正在初始化后台线程..."));
    m_progressBar->setValue(0);

    // 清除冲突的旧节点
    NodeUtils::removeDataNodeFromProject(nullptr, outName);

    m_thread = new QThread(this);
    m_worker = new PSCandidateWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, [this, daThresh, minPs, rg, az, outName, slcList]() {
        m_worker->select_candidates(daThresh, minPs, rg, az, m_projectPath, m_projectName, outName, slcList);
    });

    connect(m_worker, &PSCandidateWorker::updateProcess, this, &PS_Candidate_Dialog::onProgressUpdate);
    connect(m_worker, &PSCandidateWorker::endProcess, this, &PS_Candidate_Dialog::onProcessingFinished);
    connect(m_worker, &PSCandidateWorker::errorProcess, this, &PS_Candidate_Dialog::onError);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void PS_Candidate_Dialog::onReject()
{
    stopThread();
    reject();
}

void PS_Candidate_Dialog::stopThread()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void PS_Candidate_Dialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("进度 (%1%): %2").arg(progress).arg(message));
}

void PS_Candidate_Dialog::onError(const QString& error)
{
    QMessageBox::critical(this, "Error", QStringLiteral("计算出错: ") + error);
    m_statusLabel->setText(QStringLiteral("状态：出错中断"));
    stopThread();
}

void PS_Candidate_Dialog::onProcessingFinished()
{
    QString outName = m_outputNodeNameEdit->text();
    QString rawPath = m_projectPath;
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString h5Path = dir + "/" + outName + "/PS_candidates.h5";

    // 注册到项目树
    QList<QStandardItem*> found = m_projectModel->findItems(m_projectName);
    if (!found.isEmpty()) {
        QStandardItem* projectItem = found.first();
        QStandardItem* parentNode = NodeUtils::findOrCreateProjectNode(projectItem, outName, "mask-1.0");
        NodeUtils::findOrCreateChildItem(parentNode, "PS_candidates.h5", "mask-1.0", h5Path);
        
        // 刷新 UI 树
        emit sendCopy(m_projectModel);
    }

    m_statusLabel->setText(QStringLiteral("计算成功完成！已注册节点 %1").arg(outName));
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
    for (int i = 0; i < model->rowCount(); ++i) {
        m_projectCombo->addItem(model->item(i, 0)->text());
    }
    if (model->rowCount() > 0) {
        onProjectChanged();
    }
}

void PS_Network_Dialog::onProjectChanged()
{
    if (!m_projectModel || m_projectCombo->count() == 0) return;

    m_candidatesCombo->clear();
    m_slcCombo->clear();
    
    QString projName = m_projectCombo->currentText();
    QList<QStandardItem*> found = m_projectModel->findItems(projName);
    if (found.isEmpty()) return;

    QStandardItem* projectItem = found.first();
    m_projectPath = m_projectModel->item(projectItem->row(), 1)->text();
    m_projectName = projName;

    // 筛选子节点
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QString rtype = projectItem->child(i, 1)->text();
        QString name = projectItem->child(i, 0)->text();
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

    QString projName = m_projectCombo->currentText();
    QList<QStandardItem*> found = m_projectModel->findItems(projName);
    if (found.isEmpty()) return;
    QStandardItem* projectItem = found.first();

    // 1. 查找候选点文件路径 (PS_candidates.h5)
    QStandardItem* candNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 0)->text() == candidatesNode) {
            candNodeItem = projectItem->child(i, 0);
            break;
        }
    }
    if (!candNodeItem || candNodeItem->rowCount() == 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("候选点数据文件不存在！"));
        return;
    }
    QString candH5Path = candNodeItem->child(0, 1)->text();

    // 2. 查找配准影像集路径列表
    QStandardItem* slcNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 0)->text() == slcNode) {
            slcNodeItem = projectItem->child(i, 0);
            break;
        }
    }
    if (!slcNodeItem || slcNodeItem->rowCount() == 0) return;
    QStringList slcList;
    for (int i = 0; i < slcNodeItem->rowCount(); ++i) {
        slcList.append(slcNodeItem->child(i, 1)->text());
    }

    double maxEdge = m_maxEdgeLengthEdit->text().toDouble();
    int refRow = m_refRowEdit->text().toInt();
    int refCol = m_refColEdit->text().toInt();
    QString outName = m_outputNodeNameEdit->text();

    if (outName.isEmpty() || maxEdge <= 0.0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请输入合法参数！"));
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在初始化网络构建线程..."));
    m_progressBar->setValue(0);

    // 清除冲突的旧节点
    NodeUtils::removeDataNodeFromProject(nullptr, outName);

    m_thread = new QThread(this);
    m_worker = new PSNetworkWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, [this, maxEdge, refRow, refCol, outName, candH5Path, slcList]() {
        m_worker->build_network(maxEdge, refRow, refCol, m_projectPath, m_projectName, outName, candH5Path, slcList);
    });

    connect(m_worker, &PSNetworkWorker::updateProcess, this, &PS_Network_Dialog::onProgressUpdate);
    connect(m_worker, &PSNetworkWorker::endProcess, this, &PS_Network_Dialog::onProcessingFinished);
    connect(m_worker, &PSNetworkWorker::errorProcess, this, &PS_Network_Dialog::onError);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void PS_Network_Dialog::onReject()
{
    stopThread();
    reject();
}

void PS_Network_Dialog::stopThread()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void PS_Network_Dialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("进度 (%1%): %2").arg(progress).arg(message));
}

void PS_Network_Dialog::onError(const QString& error)
{
    QMessageBox::critical(this, "Error", QStringLiteral("计算出错: ") + error);
    m_statusLabel->setText(QStringLiteral("状态：出错中断"));
    stopThread();
}

void PS_Network_Dialog::onProcessingFinished()
{
    QString outName = m_outputNodeNameEdit->text();
    QString rawPath = m_projectPath;
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString h5Path = dir + "/" + outName + "/PS_network.h5";

    // 注册到项目树
    QList<QStandardItem*> found = m_projectModel->findItems(m_projectName);
    if (!found.isEmpty()) {
        QStandardItem* projectItem = found.first();
        QStandardItem* parentNode = NodeUtils::findOrCreateProjectNode(projectItem, outName, "mask-1.0");
        NodeUtils::findOrCreateChildItem(parentNode, "PS_network.h5", "mask-1.0", h5Path);
        
        // 刷新 UI 树
        emit sendCopy(m_projectModel);
    }

    m_statusLabel->setText(QStringLiteral("计算成功完成！已注册网络节点 %1").arg(outName));
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
    for (int i = 0; i < model->rowCount(); ++i) {
        m_projectCombo->addItem(model->item(i, 0)->text());
    }
    if (model->rowCount() > 0) {
        onProjectChanged();
    }
}

void PS_TimeSeries_Dialog::onProjectChanged()
{
    if (!m_projectModel || m_projectCombo->count() == 0) return;

    m_networkCombo->clear();
    
    QString projName = m_projectCombo->currentText();
    QList<QStandardItem*> found = m_projectModel->findItems(projName);
    if (found.isEmpty()) return;

    QStandardItem* projectItem = found.first();
    m_projectPath = m_projectModel->item(projectItem->row(), 1)->text();
    m_projectName = projName;

    // 筛选子节点 (主要是 mask-1.0，PSNetworkWorker 也是输出的 mask-1.0 类型的网络成果)
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QString rtype = projectItem->child(i, 1)->text();
        QString name = projectItem->child(i, 0)->text();
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

    QString projName = m_projectCombo->currentText();
    QList<QStandardItem*> found = m_projectModel->findItems(projName);
    if (found.isEmpty()) return;
    QStandardItem* projectItem = found.first();

    // 查找网络 H5 路径
    QStandardItem* netNodeItem = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 0)->text() == networkNodeName) {
            netNodeItem = projectItem->child(i, 0);
            break;
        }
    }
    if (!netNodeItem || netNodeItem->rowCount() == 0) return;
    QString netH5Path = netNodeItem->child(0, 1)->text();

    double cohThresh = m_coherenceThreshEdit->text().toDouble();
    double maxDef = m_maxDeformationRateEdit->text().toDouble();
    int atmosWin = m_atmosphericWindowEdit->text().toInt();
    QString outName = m_outputNodeNameEdit->text();

    if (outName.isEmpty() || cohThresh <= 0.0 || maxDef <= 0.0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请输入合法参数！"));
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在初始化时序反演分析线程..."));
    m_progressBar->setValue(0);

    // 清除冲突的旧节点
    NodeUtils::removeDataNodeFromProject(nullptr, outName);

    m_thread = new QThread(this);
    m_worker = new PSTimeSeriesWorker();
    m_worker->moveToThread(m_thread);

    QStringList fileList = QStringList() << netH5Path;

    connect(m_thread, &QThread::started, m_worker, [this, cohThresh, maxDef, atmosWin, outName, fileList]() {
        m_worker->ps_time_series(cohThresh, maxDef, atmosWin, m_projectPath, m_projectName, outName, fileList);
    });

    connect(m_worker, &PSTimeSeriesWorker::updateProcess, this, &PS_TimeSeries_Dialog::onProgressUpdate);
    connect(m_worker, &PSTimeSeriesWorker::endProcess, this, &PS_TimeSeries_Dialog::onProcessingFinished);
    connect(m_worker, &PSTimeSeriesWorker::errorProcess, this, &PS_TimeSeries_Dialog::onError);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void PS_TimeSeries_Dialog::onReject()
{
    stopThread();
    reject();
}

void PS_TimeSeries_Dialog::stopThread()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void PS_TimeSeries_Dialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("进度 (%1%): %2").arg(progress).arg(message));
}

void PS_TimeSeries_Dialog::onError(const QString& error)
{
    QMessageBox::critical(this, "Error", QStringLiteral("计算出错: ") + error);
    m_statusLabel->setText(QStringLiteral("状态：出错中断"));
    stopThread();
}

void PS_TimeSeries_Dialog::onProcessingFinished()
{
    QString outName = m_outputNodeNameEdit->text();
    QString rawPath = m_projectPath;
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString h5Path = dir + "/" + outName + "/PS_time_series.h5";

    // 注册到项目树
    QList<QStandardItem*> found = m_projectModel->findItems(m_projectName);
    if (!found.isEmpty()) {
        QStandardItem* projectItem = found.first();
        QStandardItem* parentNode = NodeUtils::findOrCreateProjectNode(projectItem, outName, "double-1.0");
        NodeUtils::findOrCreateChildItem(parentNode, "PS_time_series.h5", "double-1.0", h5Path);
        
        // 刷新 UI 树
        emit sendCopy(m_projectModel);
    }

    m_statusLabel->setText(QStringLiteral("计算成功完成！已注册时序成果节点 %1").arg(outName));
    m_progressBar->setValue(100);
    QMessageBox::information(this, "Success", QStringLiteral("PS 时序反演完成！"));
    stopThread();
    accept();
}

} // namespace QtNodes
