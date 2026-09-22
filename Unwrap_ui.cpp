#include"Unwrap_ui.h"
#include"UnwrapWorker.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<FormatConversion.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
#include <QFormLayout>
#include <Unwrap.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"

Unwrap_ui::Unwrap_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Unwrap),
    Unwrap_worker(nullptr),
    m_thread(nullptr)
{
    ui->setupUi(this);
    ui->ct_label->setHidden(1);
    ui->coherence_threshold->setHidden(1);
    this->method = 0;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    m_snaphuOptionsGroup = new QGroupBox(QStringLiteral("SNAPHU 高级参数"), this);
    auto* snaphuForm = new QFormLayout(m_snaphuOptionsGroup);
    m_snaphuTileRowsSpin = new QSpinBox(m_snaphuOptionsGroup);
    m_snaphuTileColsSpin = new QSpinBox(m_snaphuOptionsGroup);
    m_snaphuRowOverlapSpin = new QSpinBox(m_snaphuOptionsGroup);
    m_snaphuColOverlapSpin = new QSpinBox(m_snaphuOptionsGroup);
    m_snaphuTimeoutSpin = new QSpinBox(m_snaphuOptionsGroup);
    m_snaphuKeepArtifactsCheck = new QCheckBox(QStringLiteral("成功后保留现场文件"), m_snaphuOptionsGroup);
    m_snaphuTaskLabel = new QLabel(m_snaphuOptionsGroup);
    m_snaphuTaskLabel->setWordWrap(true);
    m_snaphuStatusLabel = new QLabel(QStringLiteral("状态: 未运行"), m_snaphuOptionsGroup);
    m_snaphuStatusLabel->setWordWrap(true);
    m_snaphuProcessLabel = new QLabel(QStringLiteral("并行进程: 1 (Windows SNAPHU)"), m_snaphuOptionsGroup);
    m_snaphuTileRowsSpin->setRange(1, 256);
    m_snaphuTileColsSpin->setRange(1, 256);
    m_snaphuRowOverlapSpin->setRange(0, 100000);
    m_snaphuColOverlapSpin->setRange(0, 100000);
    m_snaphuTimeoutSpin->setRange(0, 30 * 24 * 60 * 60);
    m_snaphuTimeoutSpin->setSpecialValueText(QStringLiteral("不超时"));
    m_snaphuTimeoutSpin->setSuffix(QStringLiteral(" 秒"));
    snaphuForm->addRow(QStringLiteral("分块行数"), m_snaphuTileRowsSpin);
    snaphuForm->addRow(QStringLiteral("分块列数"), m_snaphuTileColsSpin);
    snaphuForm->addRow(QStringLiteral("行重叠像素"), m_snaphuRowOverlapSpin);
    snaphuForm->addRow(QStringLiteral("列重叠像素"), m_snaphuColOverlapSpin);
    snaphuForm->addRow(QStringLiteral("最长运行时间"), m_snaphuTimeoutSpin);
    snaphuForm->addRow(m_snaphuKeepArtifactsCheck);
    snaphuForm->addRow(m_snaphuProcessLabel);
    snaphuForm->addRow(m_snaphuTaskLabel);
    snaphuForm->addRow(m_snaphuStatusLabel);
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->progressBar), m_snaphuOptionsGroup);
    connect(m_snaphuTileRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &Unwrap_ui::updateSnaphuOptionWidgets);
    connect(m_snaphuTileColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &Unwrap_ui::updateSnaphuOptionWidgets);
    updateSnaphuOptionWidgets();
    connect(ui->SPDButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
    connect(ui->MCFButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
    connect(ui->SnaphuButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
    connect(ui->Q_MButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
}
Unwrap_ui::~Unwrap_ui()
{
    // Core still owns the borrowed callback userData while SNAPHU is running.
    // A parented QThread must therefore finish before this widget is destroyed.
    if (Unwrap_worker) {
        Unwrap_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->itemText(i));
            if (project)
                project->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    Unwrap_worker = nullptr;
    m_thread = nullptr;
}

void Unwrap_ui::updateProcess(int value, QString information)
{
    if (value < 0) {
        ui->progressBar->setRange(0, 0);
        ui->progressBar->setFormat(information);
        return;
    }
    if (ui->progressBar->minimum() == 0 && ui->progressBar->maximum() == 0) {
        ui->progressBar->setRange(0, 100);
    }
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Unwrap_ui::onSnaphuRunEvent(const SnaphuRunEventInfo& event)
{
    if (event.type == SNAPHU_RUN_EVENT_PREPARED) {
        if (m_snaphuTaskLabel) {
            m_snaphuTaskLabel->setText(QStringLiteral("现场: %1\n配置: %2")
                .arg(event.taskDirectory, event.configPath));
        }
        if (m_snaphuStatusLabel) m_snaphuStatusLabel->setText(QStringLiteral("状态: 已准备，等待 SNAPHU 启动"));
        InSARLogManager::LogInfo("Unwrap_ui", QStringLiteral("SNAPHU staging: %1; config: %2")
            .arg(event.taskDirectory, event.configPath));
        return;
    }
    if (event.type == SNAPHU_RUN_EVENT_STARTED || event.type == SNAPHU_RUN_EVENT_HEARTBEAT) {
        QStringList metrics;
        metrics.append(QStringLiteral("运行 %1 s").arg(event.elapsedMilliseconds / 1000));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_CPU_TIME)
            ? QStringLiteral("CPU %1 s").arg(event.totalCpuMilliseconds / 1000) : QStringLiteral("CPU 未知"));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_PEAK_JOB_MEMORY)
            ? QStringLiteral("内存 %1 MiB").arg(event.peakJobMemoryBytes / (1024 * 1024)) : QStringLiteral("内存未知"));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_READ_BYTES)
            ? QStringLiteral("读取 %1 MiB").arg(event.readBytes / (1024 * 1024)) : QStringLiteral("读取未知"));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_WRITE_BYTES)
            ? QStringLiteral("写入 %1 MiB").arg(event.writeBytes / (1024 * 1024)) : QStringLiteral("写入未知"));
        const QString details = metrics.join(QStringLiteral(", "));
        if (m_snaphuStatusLabel) {
            m_snaphuStatusLabel->setText(QStringLiteral("状态: SNAPHU 正在运行，内部进度未知\n%1").arg(details));
        }
        updateProcess(-1, QStringLiteral("SNAPHU 运行中：%1 (%2)").arg(event.message, details));
    } else if (event.type == SNAPHU_RUN_EVENT_COMPLETED) {
        ui->progressBar->setRange(0, 100);
    } else if (event.type == SNAPHU_RUN_EVENT_LOG || event.type == SNAPHU_RUN_EVENT_WARNING) {
        InSARLogManager::LogInfo("Unwrap_ui", QStringLiteral("SNAPHU: %1").arg(event.message));
    }
}
void Unwrap_ui::endProcess()
{
    QString transactionError;
    QStringList workerPaths;
    QStringList finalPaths;
    for (const UnwrapFileResult& result : m_pendingUnwrapResults) {
        workerPaths.append(result.absolutePath);
    }
    if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, workerPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        !NodeUtils::completeOutputTransactionWithoutMetadata(m_outputTransaction, &transactionError)) {
        if (m_thread && m_thread->isRunning()) {
            m_thread->quit();
            m_thread->wait();
        }
        releaseStoppedThread();
        abandonOutputTransaction(transactionError.isEmpty()
            ? QStringLiteral("解缠输出事务提交失败") : transactionError);
        ChangeVision(true);
        ui->progressBar->hide();
        QMessageBox::critical(this, QStringLiteral("Error"), transactionError.isEmpty()
            ? QStringLiteral("解缠输出事务提交失败") : transactionError);
        return;
    }
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
    }
    releaseStoppedThread();
    ui->progressBar->hide();
    this->close();
}
void Unwrap_ui::endThread()
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
    }
    releaseStoppedThread();
}
void Unwrap_ui::StopThread()
{
    if (Unwrap_worker != NULL)
    {
        Unwrap_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
    }
}
void Unwrap_ui::onWorkerError(const QString& error)
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    releaseStoppedThread();
    abandonOutputTransaction(error);
    ui->progressBar->hide();
    ChangeVision(true);
    QMessageBox::critical(this, QStringLiteral("Error"), error);
}
void Unwrap_ui::onWorkerCancelled()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    releaseStoppedThread();
    abandonOutputTransaction(QStringLiteral("cancelled"));
    ui->progressBar->hide();
    ChangeVision(true);
}
void Unwrap_ui::onUnwrapFileGenerated(const UnwrapFileResult& result)
{
    m_pendingUnwrapResults.append(result);
}
void Unwrap_ui::abandonOutputTransaction(const QString& reason)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, reason);
}
void Unwrap_ui::releaseStoppedThread()
{
    if (!m_thread || m_thread->isRunning()) {
        return;
    }
    m_thread->deleteLater();
    m_thread = nullptr;
    Unwrap_worker = nullptr;
}
void Unwrap_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void Unwrap_ui::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->file_name->setDisabled(0);
        ui->SPDButton->setDisabled(0);
        ui->MCFButton->setDisabled(0);
        ui->Q_MButton->setDisabled(0);
        ui->SnaphuButton->setDisabled(0);
        ui->coherence_threshold->setDisabled(0);
        if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->file_name->setDisabled(1);
        ui->SPDButton->setDisabled(1);
        ui->MCFButton->setDisabled(1);
        ui->Q_MButton->setDisabled(1);
        ui->SnaphuButton->setDisabled(1);
        ui->coherence_threshold->setDisabled(1);
        if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void Unwrap_ui::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    if (!copy || copy->rowCount() < 1 || copy->columnCount() < 2 ||
        !copy->item(0, 0) || !copy->item(0, 1))
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("当前没有可用工程，请先新建或打开工程。"));
        return;
    }
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (!projectItem) continue;
        ui->comboBox->addItem(projectItem->text());
        projectItem->setStatusTip(IN_PROCESS);
    }
    
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;  
    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (projectItem && projectItem->rowCount() != 0)
        {
            count = projectItem->rowCount();
            project = projectItem;
            ui->comboBox->setCurrentIndex(ui->comboBox->findText(projectItem->text()));
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0 )
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据，请确保已经生成过干涉相位或已完成滤波！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);
}
void Unwrap_ui::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        /*this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();*/
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* pathItem = copy->item(project->row(), 1);
        if (!pathItem) {
            QMessageBox::warning(this, "Warning!", QStringLiteral("当前工程路径信息缺失，请重新打开工程。"));
            return;
        }
        this->save_path = pathItem->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_2->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0)
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Unwrap_ui::on_comboBox_2_currentIndexChanged()
{
    /*QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
        {
            QStandardItem* node = project->child(i, 0);
            int count = 0;
            for (int j = 0; j < node->rowCount(); j++)
            {
                if (node->child(j, 0)->toolTip() == "phase")
                    count++;
            }
            this->image_number = count;
            break;
        }
    }*/
}

void Unwrap_ui::on_buttonBox_accepted()
{
    bool bFlag = false;
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据，请先进行滤波或更换工程！"));
        return;
    }
    if (ui->SPDButton->isChecked())
    {
        this->method = 1;
    }
    else if (ui->MCFButton->isChecked())
    {
        this->method = 2;
    }
    else if (ui->SnaphuButton->isChecked())
    {
        this->method = 3;
    }
    else if (ui->Q_MButton->isChecked())
    {
        this->method = 4;
        if (ui->coherence_threshold->text().isEmpty())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入0-1的相干系数阈值!"));
            return;
        }
       
        double threshold = ui->coherence_threshold->text().toDouble(&bFlag);
        if (!bFlag)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("请确认相干系数阈值为小数!"));
            return;
        }
        else if (threshold < 0 || threshold >1)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("请确认相干系数阈值在0-1!"));
            return;
        }
    } 
    if (this->method == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请选择解缠方法!"));
        return;
    }
    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入保存解缠相位的文件夹名称！"));
        return;
    }
    bFlag = ui->file_name->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }

    QStringList phasePaths;
    if (copy) {
        const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
        if (!projects.isEmpty()) {
            QStandardItem* project = projects.first();
            for (int i = 0; i < project->rowCount(); ++i) {
                QStandardItem* node = project->child(i, 0);
                if (!node || node->text() != ui->comboBox_2->currentText()) {
                    continue;
                }
                for (int j = 0; j < node->rowCount(); ++j) {
                    QStandardItem* typeItem = node->child(j, 0);
                    QStandardItem* pathItem = node->child(j, 1);
                    if (typeItem && pathItem && typeItem->toolTip() == "phase" && !pathItem->text().isEmpty()) {
                        phasePaths.append(pathItem->text());
                    }
                }
                break;
            }
        }
    }
    if (phasePaths.isEmpty()) {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("No phase input paths are available."));
        return;
    }

    m_preparedOutputPaths.clear();
    for (const QString& phasePath : phasePaths) {
        const QString outputName = QFileInfo(phasePath).baseName() + QStringLiteral("_unwrapped.h5");
        m_preparedOutputPaths.append(QDir(save_path).filePath(ui->file_name->text() + "/" + outputName));
    }
    m_pendingUnwrapResults.clear();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(save_path, ui->file_name->text(), m_preparedOutputPaths,
                                           phasePaths, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(this))) {
        QMessageBox::critical(this, QStringLiteral("Error"), transactionError);
        return;
    }

    m_thread = new QThread(this);
    Unwrap_worker = new UnwrapWorker();
    Unwrap_worker->moveToThread(m_thread);

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &Unwrap_ui::operate, Unwrap_worker, &UnwrapWorker::Unwrap, Qt::QueuedConnection);
    connect(Unwrap_worker, &UnwrapWorker::updateProcess, this, &Unwrap_ui::updateProcess);
    connect(Unwrap_worker, &UnwrapWorker::snaphuRunEvent, this, &Unwrap_ui::onSnaphuRunEvent, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, Unwrap_worker, &QObject::deleteLater);
    connect(Unwrap_worker, &UnwrapWorker::endProcess, this, &Unwrap_ui::endProcess);
    connect(Unwrap_worker, &UnwrapWorker::unwrapFileGenerated, this, &Unwrap_ui::onUnwrapFileGenerated);
    connect(Unwrap_worker, &UnwrapWorker::errorProcess, this, &Unwrap_ui::onWorkerError);
    connect(Unwrap_worker, &UnwrapWorker::errorProcess, m_thread, &QThread::quit);
    connect(Unwrap_worker, &UnwrapWorker::cancelled, this, &Unwrap_ui::onWorkerCancelled);
    connect(Unwrap_worker, &UnwrapWorker::cancelled, m_thread, &QThread::quit);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Unwrap_ui::StopThread);
    
    m_thread->start();
    ChangeVision(false);
    emit operate(this->method, ui->coherence_threshold->text().toDouble(), this->save_path,
                 m_outputTransaction.stagingName, phasePaths, snaphuOptions());
}

void Unwrap_ui::on_buttonBox_rejected()
{
    if (m_thread && m_thread->isRunning()) {
        StopThread();
        updateProcess(-1, QStringLiteral("正在取消 SNAPHU，等待外部进程退出…"));
        return;
    }
    this->close();
}

void Unwrap_ui::Change_Setting()
{
    if (ui->SPDButton->isChecked())
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 1;
    }
    else if (ui->MCFButton->isChecked())
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 2;
    }
    else if (ui->SnaphuButton->isChecked())
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 3;
        if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setVisible(true);
    }
    else if (ui->Q_MButton->isChecked())
    {
        ui->ct_label->setHidden(0);
        ui->coherence_threshold->setHidden(0);
        this->method = 4;
    }
    else
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 0;
    }
    if (m_snaphuOptionsGroup && !ui->SnaphuButton->isChecked()) m_snaphuOptionsGroup->setVisible(false);
}

SnaphuUiOptions Unwrap_ui::snaphuOptions() const
{
    SnaphuUiOptions options;
    options.tileRows = static_cast<quint32>(m_snaphuTileRowsSpin->value());
    options.tileCols = static_cast<quint32>(m_snaphuTileColsSpin->value());
    options.rowOverlap = static_cast<quint32>(m_snaphuRowOverlapSpin->value());
    options.colOverlap = static_cast<quint32>(m_snaphuColOverlapSpin->value());
    options.wallTimeoutMilliseconds = static_cast<quint64>(m_snaphuTimeoutSpin->value()) * 1000;
    options.keepArtifactsOnSuccess = m_snaphuKeepArtifactsCheck->isChecked();
    return options;
}

void Unwrap_ui::updateSnaphuOptionWidgets()
{
    if (!m_snaphuTileRowsSpin) return;
    const bool tiled = m_snaphuTileRowsSpin->value() > 1 || m_snaphuTileColsSpin->value() > 1;
    m_snaphuRowOverlapSpin->setEnabled(tiled);
    m_snaphuColOverlapSpin->setEnabled(tiled);
    m_snaphuRowOverlapSpin->setMinimum(tiled ? 50 : 0);
    m_snaphuColOverlapSpin->setMinimum(tiled ? 50 : 0);
    if (tiled) {
        if (m_snaphuRowOverlapSpin->value() < 50) m_snaphuRowOverlapSpin->setValue(200);
        if (m_snaphuColOverlapSpin->value() < 50) m_snaphuColOverlapSpin->setValue(200);
    } else {
        m_snaphuRowOverlapSpin->setValue(0);
        m_snaphuColOverlapSpin->setValue(0);
    }
    if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setVisible(ui->SnaphuButton->isChecked());
}
