#include"SBAS_time_series_analysis.h"
#include"ui_SbasTimeSeriesAnalysis.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<QFileDialog>
#include<Utils.h>
#include<qmessagebox.h>
#include<QFile>
#include<QFileInfo>
#include<QDir>
#include<QThread>
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "tinyxml.h"
#include <FormatConversion.h>
SBAS_time_series_analysis::SBAS_time_series_analysis(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::SbasTimeSeriesAnalysis)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();
    ui->radioButton_Delaunay_MCF->setChecked(true);
    ui->doubleSpinBox_temporal_thresh->setMinimum(0.0);
    ui->doubleSpinBox_temporal_thresh->setMaximum(2000.0);
    ui->doubleSpinBox_temporal_thresh->setValue(200.0);
    ui->doubleSpinBox_temporal_thresh_low->setMinimum(0.0);
    ui->doubleSpinBox_temporal_thresh_low->setMaximum(1000.0);
    ui->doubleSpinBox_temporal_thresh_low->setValue(0.0);
    ui->doubleSpinBox_spatial_thresh->setMinimum(0.0);
    ui->doubleSpinBox_spatial_thresh->setMaximum(10000.0);
    ui->doubleSpinBox_spatial_thresh->setValue(500.0);
    ui->doubleSpinBox_reflattening_coh_thresh->setMinimum(0.0);
    ui->doubleSpinBox_reflattening_coh_thresh->setMaximum(1.0);
    ui->doubleSpinBox_reflattening_coh_thresh->setValue(0.8);
    ui->doubleSpinBox_reflattening_def_thresh->setMinimum(0.0);
    ui->doubleSpinBox_reflattening_def_thresh->setValue(0.01);
    ui->spinBox_multilook_az->setMinimum(2);
    ui->spinBox_multilook_az->setMaximum(50);
    ui->spinBox_multilook_rg->setMinimum(2);
    ui->spinBox_multilook_rg->setMaximum(50);
    ui->spinBox_multilook_az->setValue(4);
    ui->spinBox_multilook_rg->setValue(4);
    ui->doubleSpinBox_coherence_thresh->setMinimum(0.0);
    ui->doubleSpinBox_coherence_thresh->setMaximum(1.0);
    ui->doubleSpinBox_coherence_thresh->setValue(0.5);
    ui->doubleSpinBox_temporal_coherence_thresh->setMinimum(0.0);
    ui->doubleSpinBox_temporal_coherence_thresh->setMaximum(1.0);
    ui->doubleSpinBox_temporal_coherence_thresh->setValue(0.6);
    ui->doubleSpinBox_Goldstein_alpha->setMinimum(0.0);
    ui->doubleSpinBox_Goldstein_alpha->setMaximum(1.0);
    ui->doubleSpinBox_Goldstein_alpha->setValue(0.8);
}
SBAS_time_series_analysis::~SBAS_time_series_analysis()
{
    StopThread();
    if (copy)
    {
        if (QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText()))
            project->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    SBAS_time_series_analysis_thread = NULL;
}

void SBAS_time_series_analysis::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void SBAS_time_series_analysis::endProcess()
{
    QString transactionError;
    if (!commitOutputTransaction(&transactionError)) {
        rollbackOutputTransaction(transactionError);
        QMessageBox::warning(this, "Error", transactionError);
        StopThread();
        return;
    }
    SBAS_time_series_analysis_thread->thread()->quit();
    SBAS_time_series_analysis_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void SBAS_time_series_analysis::endThread()
{
    SBAS_time_series_analysis_thread->thread()->quit();
    SBAS_time_series_analysis_thread->thread()->wait();
}
void SBAS_time_series_analysis::StopThread()
{
    if (SBAS_time_series_analysis_thread != NULL)
    {
        SBAS_time_series_analysis_thread->StopProcess();
        if (SBAS_time_series_analysis_thread->thread()->isRunning())
        {
            SBAS_time_series_analysis_thread->thread()->requestInterruption();
            SBAS_time_series_analysis_thread->thread()->quit();
            SBAS_time_series_analysis_thread->thread()->wait();
        }
    }
    rollbackOutputTransaction(QStringLiteral("legacy SBAS dialog stopped"));
}

void SBAS_time_series_analysis::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void SBAS_time_series_analysis::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    ui->comboBox_project->clear();
    ui->comboBox_srcNode->clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2 ||
        !model->item(0, 0) || !model->item(0, 1))
    {
        this->save_path.clear();
        return;
    }
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (!projectItem) continue;
        ui->comboBox_project->addItem(projectItem->text());
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
            ui->comboBox_project->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        ui->comboBox_srcNode->clear();
        return;
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    for (int i = 0; i < count; i++)
    {
        QStandardItem* typeItem = project->child(i, 1);
        QStandardItem* nameItem = project->child(i, 0);
        if (typeItem && nameItem && typeItem->text() == QString("complex-3.0"))
        {
            ui->comboBox_srcNode->addItem(nameItem->text());
            if (!isnodefound)
            {
                node = nameItem;
                isnodefound = true;
            }

        }
    }
    if (!node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
        ui->comboBox_srcNode->clear();
        return;
    }
    ui->comboBox_srcNode->setCurrentIndex(0);
}
void SBAS_time_series_analysis::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        bool isnodefound = false;
        QStandardItem* node = NULL;
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project) {
            ui->comboBox_srcNode->clear();
            QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* pathItem = copy->item(project->row(), 1);
        this->save_path = pathItem ? pathItem->text() : QString();
        ui->comboBox_srcNode->clear();
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* nameItem = project->child(i, 0);
            QStandardItem* rankItem = project->child(i, 1);
            if (nameItem && rankItem && rankItem->text() == QString("complex-3.0"))
            {
                ui->comboBox_srcNode->addItem(nameItem->text());
                if (!isnodefound)
                {
                    node = nameItem;
                    isnodefound = true;
                }

            }
        }
        if (!isnodefound)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
            ui->comboBox_srcNode->clear();
            return;
        }
    }
}



void SBAS_time_series_analysis::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project->setDisabled(0);
        ui->comboBox_srcNode->setDisabled(0);
        ui->lineEdit_dstNode->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->doubleSpinBox_temporal_thresh->setDisabled(0);
        ui->doubleSpinBox_temporal_thresh_low->setDisabled(0);
        ui->doubleSpinBox_spatial_thresh->setDisabled(0);
        ui->doubleSpinBox_coherence_thresh->setDisabled(0);
        ui->doubleSpinBox_temporal_coherence_thresh->setDisabled(0);
        ui->doubleSpinBox_Goldstein_alpha->setDisabled(0);
        ui->doubleSpinBox_reflattening_coh_thresh->setDisabled(0);
        ui->doubleSpinBox_reflattening_def_thresh->setDisabled(0);
        ui->radioButton_Delaunay_MCF->setDisabled(0);
        ui->radioButton_MCF->setDisabled(0);
        ui->radioButton_SNAPHU->setDisabled(0);
        ui->spinBox_multilook_az->setDisabled(0);
        ui->spinBox_multilook_rg->setDisabled(0);
    }
    else
    {
        ui->comboBox_project->setDisabled(1);
        ui->comboBox_srcNode->setDisabled(1);
        ui->lineEdit_dstNode->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->doubleSpinBox_temporal_thresh->setDisabled(1);
        ui->doubleSpinBox_temporal_thresh_low->setDisabled(1);
        ui->doubleSpinBox_spatial_thresh->setDisabled(1);
        ui->doubleSpinBox_coherence_thresh->setDisabled(1);
        ui->doubleSpinBox_Goldstein_alpha->setDisabled(1);
        ui->doubleSpinBox_temporal_coherence_thresh->setDisabled(1);
        ui->doubleSpinBox_reflattening_coh_thresh->setDisabled(1);
        ui->doubleSpinBox_reflattening_def_thresh->setDisabled(1);
        ui->radioButton_Delaunay_MCF->setDisabled(1);
        ui->radioButton_MCF->setDisabled(1);
        ui->radioButton_SNAPHU->setDisabled(1);
        ui->spinBox_multilook_az->setDisabled(1);
        ui->spinBox_multilook_rg->setDisabled(1);
    }


}


void SBAS_time_series_analysis::on_comboBox_srcNode_currentIndexChanged()
{
    
    if (ui->comboBox_srcNode->count() > 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project) {
            QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* childItem = project->child(i, 0);
            if (childItem && childItem->text() == ui->comboBox_srcNode->currentText())
            {
                node = childItem; break;
            }
        }

        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            return;
        }
    }
}

void SBAS_time_series_analysis::on_buttonbrowse_triggered()
{
    QString save_path = QFileDialog::getSaveFileName(this,
        QStringLiteral("csv另存为"),
        "/",
        "*.csv");
    ui->csv_path->setText(save_path);
}

void SBAS_time_series_analysis::on_buttonBox_accepted()
{
    bool bFlag = false;
    QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
    if (!project) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
        return;
    }
    if (project->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    //防重名检查
    if (ui->lineEdit_dstNode->text().isEmpty()) return;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* childItem = project->child(i, 0);
        if (childItem && ui->lineEdit_dstNode->text() == childItem->text())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }
    if (ui->csv_path->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入新建工程名称（该名称应为数字、字母及下划线的组合）！"));
        return;
    }
    bFlag = ui->csv_path->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意工程名称应当为数字、字母及下划线的组合！"));
        return;
    }
    if (ui->radioButton_MCF->isChecked())
    {
        this->method = 3;
    }
    else if (ui->radioButton_SNAPHU->isChecked())
    {
        this->method = 2;
    }
    else
    {
        this->method = 1;
    }
    QStandardItem* project_item = project;
    QStandardItem* image = NULL;
    for (int i = 0; i < project_item->rowCount(); i++)
    {
        QStandardItem* childItem = project_item->child(i, 0);
        if (childItem && childItem->text() == ui->comboBox_srcNode->currentText())
        {
            image = childItem;
            break;
        }
    }
    if (!image) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选数据节点不存在，请重新选择。"));
        return;
    }
    QStringList filePaths;
    for (int i = 0; i < image->rowCount(); i++)
    {
        QStandardItem* pathItem = image->child(i, 1);
        if (pathItem && !pathItem->text().isEmpty())
            filePaths.append(pathItem->text());
    }
    if (filePaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选数据节点不包含有效文件路径。"));
        return;
    }

    QStandardItem* projectPathItem = copy ? copy->item(project_item->row(), 1) : nullptr;
    if (!projectPathItem || projectPathItem->text().isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("工程路径无效，请重新打开工程。"));
        return;
    }
    QString projPath = projectPathItem->text();
    m_activeProjectRoot = projPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(projPath).absolutePath() : projPath;
    m_activeProjectName = ui->comboBox_project->currentText();
    m_activeDstNode = ui->lineEdit_dstNode->text().trimmed();
    m_activeInputPaths = filePaths;
    m_activeOutputPaths = QStringList() << QDir(m_activeProjectRoot).absoluteFilePath(
        m_activeDstNode + "/SBAS_time_series.h5");
    m_pendingResult = SBASTimeSeriesResult();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_activeProjectRoot, m_activeDstNode,
                                           m_activeOutputPaths, m_activeInputPaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(this))) {
        QMessageBox::warning(this, "Error", transactionError);
        return;
    }

    SBAS_time_series_analysis_thread = new SBASTimeSeriesWorker();
    QThread* thread = new QThread(this);
    SBAS_time_series_analysis_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    
    SBASTimeSeriesWorker* const worker = SBAS_time_series_analysis_thread.data();
    connect(this, &SBAS_time_series_analysis::operate, worker,
            [worker](double temporalThreshLow, double temporalThresh, double spatialThresh,
                     int multilookRg, int multilookAz, int unwrapMethod, double alpha,
                     double coherenceThresh, double temporalCoherenceThresh,
                     double refinementCohThresh, double refinementDefThresh,
                     QString projectPath, QString projectName, QString dstNode, QString csvPath,
                     QStringList filePaths, bool outputDirectoryIsStaging) {
        worker->SBAS_time_series(temporalThreshLow, temporalThresh, spatialThresh,
                                 multilookRg, multilookAz, unwrapMethod, alpha,
                                 coherenceThresh, temporalCoherenceThresh,
                                 refinementCohThresh, refinementDefThresh,
                                 projectPath, projectName, dstNode, csvPath, filePaths,
                                 outputDirectoryIsStaging, QString(), nullptr);
    }, Qt::QueuedConnection);
    connect(SBAS_time_series_analysis_thread, &SBASTimeSeriesWorker::sbasGenerated, this, [this](const SBASTimeSeriesResult& res) {
        m_pendingResult = res;
    });
    connect(SBAS_time_series_analysis_thread, &SBASTimeSeriesWorker::updateProcess, this, &SBAS_time_series_analysis::updateProcess);
    connect(thread, &QThread::finished, SBAS_time_series_analysis_thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(SBAS_time_series_analysis_thread, &SBASTimeSeriesWorker::endProcess, this, &SBAS_time_series_analysis::endProcess);
    connect(SBAS_time_series_analysis_thread, &SBASTimeSeriesWorker::errorProcess, this, [this](QString err) {
        rollbackOutputTransaction(err);
        QMessageBox::warning(this, "Error", err);
        StopThread();
    });
    connect(SBAS_time_series_analysis_thread, &SBASTimeSeriesWorker::cancelled, this, [this]() {
        rollbackOutputTransaction(QStringLiteral("cancelled"));
        StopThread();
    });
    connect(this, &QWidget::destroyed, this, &SBAS_time_series_analysis::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SBAS_time_series_analysis::StopThread);
    
    thread->start();
    ChangeVision(false);

    const QString stagingNode = m_outputTransaction.stagingName;

    emit operate(
        ui->doubleSpinBox_temporal_thresh_low->value(),
        ui->doubleSpinBox_temporal_thresh->value(),
        ui->doubleSpinBox_spatial_thresh->value(),
        ui->spinBox_multilook_rg->value(),
        ui->spinBox_multilook_az->value(),
        this->method,
        ui->doubleSpinBox_Goldstein_alpha->value(),
        ui->doubleSpinBox_coherence_thresh->value(),
        ui->doubleSpinBox_temporal_coherence_thresh->value(),
        ui->doubleSpinBox_reflattening_coh_thresh->value(),
        ui->doubleSpinBox_reflattening_def_thresh->value(),
        m_activeProjectRoot,
        ui->comboBox_project->currentText(),
        stagingNode,
        ui->csv_path->text(),
        filePaths,
        true
    );

}

bool SBAS_time_series_analysis::commitOutputTransaction(QString* errorMessage)
{
    if (m_pendingResult.dstNode != m_outputTransaction.stagingName ||
        m_pendingResult.timesSeriesH5Path.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, errorMessage) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("mask") << QStringLiteral("defomation_velocity")
                          << QStringLiteral("deformation_time_series"), errorMessage) ||
        !NodeUtils::workerOutputsMatchManifest(m_activeOutputPaths,
            QStringList() << m_pendingResult.timesSeriesH5Path, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("SBAS worker did not return the expected staging output.");
        }
        return false;
    }

    const QString xmlPath = QDir(m_activeProjectRoot).absoluteFilePath(m_activeProjectName);
    XMLFile xml;
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0 ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, m_activeOutputPaths, errorMessage) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, &xml, xmlPath, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Unable to prepare SBAS output metadata.");
        }
        return false;
    }

    const QString relativePath = QStringLiteral("/%1/SBAS_time_series.h5").arg(m_activeDstNode);
    TiXmlElement* root = nullptr;
    if (xml.get_root(root) < 0 || !root) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to read project XML root for SBAS output.");
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            errorMessage ? *errorMessage : QStringLiteral("missing XML root"),
                                            &xml);
        return false;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_activeDstNode == QString::fromUtf8(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    if (xml.XMLFile_add_SBAS(m_activeDstNode.toStdString().c_str(), "SBAS_time_series",
                             relativePath.toStdString().c_str()) < 0 ||
        !NodeUtils::saveProjectXmlAtomically(&xml, xmlPath, errorMessage) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Unable to commit SBAS output metadata.");
        }
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            errorMessage ? *errorMessage : QStringLiteral("metadata commit failed"),
                                            &xml);
        return false;
    }

    if (copy) {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, m_activeProjectName);
        if (project) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                project, m_activeDstNode, "SBAS-1.0", FOLDER_ICON);
            if (outputNode) {
                outputNode->setToolTip(m_activeProjectName);
                NodeUtils::findOrCreateChildItem(outputNode, "SBAS_time_series", "SBAS",
                                                  m_activeOutputPaths.first(), IMAGEDATA_ICON);
            }
        } else {
            InSARLogManager::LogError("SBAS_time_series_analysis",
                QStringLiteral("处理完成后未找到工程“%1”，已跳过项目树发布。").arg(m_activeProjectName));
        }
        emit sendCopy(copy);
    }
    return true;
}

void SBAS_time_series_analysis::rollbackOutputTransaction(const QString& reason)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, reason);
}

void SBAS_time_series_analysis::on_buttonBox_rejected()
{
    this->close();
}
