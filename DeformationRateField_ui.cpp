#include "DeformationRateField_ui.h"
#include "ui_DeformationRateField.h"
#include "icon_source.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include <FormatConversion.h>
#include <QMessageBox>
#include <QThread>
#include <QCoreApplication>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include "tinyxml.h"

DeformationRateField_ui::DeformationRateField_ui(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::DeformationRateField)
    , copy(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();

    ui->comboBox_modelType->addItem(QStringLiteral("线性拟合"), 1);
    ui->comboBox_modelType->addItem(QStringLiteral("二次多项式"), 2);

    ui->comboBox_confidenceLevel->addItem("90%", 0.90);
    ui->comboBox_confidenceLevel->addItem("95%", 0.95);
    ui->comboBox_confidenceLevel->addItem("99%", 0.99);
    ui->comboBox_confidenceLevel->setCurrentIndex(1); // 95%

    ui->comboBox_colorMap->addItem(QStringLiteral("蓝-白-红"), 0);
    ui->comboBox_colorMap->addItem(QStringLiteral("热力图"), 1);
    ui->comboBox_colorMap->addItem(QStringLiteral("彩虹色"), 2);

    connect(ui->comboBox_project, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DeformationRateField_ui::on_comboBox_project_currentIndexChanged);
}

DeformationRateField_ui::~DeformationRateField_ui()
{
    if (copy && ui->comboBox_project->count() > 0) {
        auto items = copy->findItems(ui->comboBox_project->currentText());
        if (!items.isEmpty() && items[0]) {
            items[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    StopThread();
    delete ui;
}

void DeformationRateField_ui::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    ui->comboBox_project->clear();
    ui->comboBox_srcNode->clear();
    this->save_path.clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2) return;

    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        QStandardItem* pathItem = model->item(i, 1);
        if (!projectItem || !pathItem) continue;
        ui->comboBox_project->addItem(projectItem->text());
        projectItem->setStatusTip(IN_PROCESS);
    }

    if (ui->comboBox_project->count() < 1) return;
    updateSrcNodeCombo();
}

void DeformationRateField_ui::on_comboBox_project_currentIndexChanged()
{
    updateSrcNodeCombo();
}

void DeformationRateField_ui::updateSrcNodeCombo()
{
    ui->comboBox_srcNode->clear();
    if (!copy || ui->comboBox_project->count() == 0) return;

    auto items = copy->findItems(ui->comboBox_project->currentText());
    if (items.isEmpty() || !items[0]) return;
    QStandardItem* project = items[0];
    QStandardItem* pathItem = copy->item(project->row(), 1);
    if (!pathItem) return;
    this->save_path = pathItem->text();

    for (int i = 0; i < project->rowCount(); i++) {
        QStandardItem* child_col0 = project->child(i, 0);
        QStandardItem* child_col1 = project->child(i, 1);
        if (child_col0 && child_col1 && child_col1->text() == "SBAS-1.0") {
            for (int j = 0; j < child_col0->rowCount(); ++j) {
                QStandardItem* leaf_col0 = child_col0->child(j, 0);
                if (leaf_col0) {
                    ui->comboBox_srcNode->addItem(QString("%1/%2").arg(child_col0->text()).arg(leaf_col0->text()));
                }
            }
        }
    }
}

QStringList DeformationRateField_ui::getSelectedFilePaths()
{
    QStringList paths;
    if (!copy || ui->comboBox_srcNode->count() == 0) return paths;

    QString current = ui->comboBox_srcNode->currentText();
    QStringList parts = current.split('/');
    if (parts.size() < 2) return paths;

    auto items = copy->findItems(ui->comboBox_project->currentText());
    if (items.isEmpty() || !items[0]) return paths;
    QStandardItem* project = items[0];

    for (int i = 0; i < project->rowCount(); i++) {
        QStandardItem* child_col0 = project->child(i, 0);
        if (child_col0 && child_col0->text() == parts[0]) {
            for (int j = 0; j < child_col0->rowCount(); ++j) {
                QStandardItem* leaf_col0 = child_col0->child(j, 0);
                QStandardItem* leaf_col1 = child_col0->child(j, 1);
                if (leaf_col0 && leaf_col0->text() == parts[1] && leaf_col1) {
                    paths.append(leaf_col1->text());
                    return paths;
                }
            }
        }
    }
    return paths;
}

void DeformationRateField_ui::updateProcess(int progress, QString message)
{
    ui->progressBar->setValue(progress);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(message).arg(progress));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void DeformationRateField_ui::endProcess()
{
    QString error;
    if (!commitOutputTransaction(&error)) {
        QMessageBox::warning(this, "Error", error);
        StopThread();
        ui->progressBar->hide();
        setControlsEnabled(true);
        return;
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
    ui->progressBar->hide();
    this->close();
}

void DeformationRateField_ui::StopThread()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
    rollbackOutputTransaction(QStringLiteral("cancelled"));
}

void DeformationRateField_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void DeformationRateField_ui::handleResults(const QString& dstNode, const QString& outputH5Path)
{
    if (dstNode != m_activeDstNode || outputH5Path.isEmpty()) {
        return;
    }
    m_workerOutputPaths.append(outputH5Path);
}

bool DeformationRateField_ui::commitOutputTransaction(QString* errorMessage)
{
    if (m_workerOutputPaths.size() != 1 ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Rate field worker did not return the expected staging output.");
        }
        return false;
    }

    QStringList requiredDatasets = QStringList()
        << QStringLiteral("velocity_std") << QStringLiteral("velocity_lower")
        << QStringLiteral("velocity_upper") << QStringLiteral("quality_mask") << QStringLiteral("mask");
    if (m_activeModelType == 2) {
        requiredDatasets << QStringLiteral("velocity_nonlinear") << QStringLiteral("acceleration")
                         << QStringLiteral("acceleration_std");
    }
    if (!NodeUtils::validateStagedH5Datasets(m_outputTransaction, requiredDatasets, errorMessage) ||
        !NodeUtils::workerOutputsMatchManifest(m_activeOutputPaths, m_workerOutputPaths, errorMessage)) {
        return false;
    }

    const QString xmlPath = QDir(m_activeProjectRoot).absoluteFilePath(m_activeProjectName);
    XMLFile xml;
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to load project XML for rate field output.");
        return false;
    }

    QStringList finalPaths;
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, errorMessage) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, &xml, xmlPath, errorMessage)) {
        return false;
    }

    TiXmlElement* root = nullptr;
    if (xml.get_root(root) < 0 || !root) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to read project XML root for rate field output.");
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

    const QString relativePath = QStringLiteral("/%1/DeformationRateField.h5").arg(m_activeDstNode);
    if (xml.XMLFile_add_SBAS(m_activeDstNode.toStdString().c_str(), "DeformationRateField",
                             relativePath.toStdString().c_str()) < 0 ||
        !NodeUtils::saveProjectXmlAtomically(&xml, xmlPath, errorMessage) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Unable to commit rate field output metadata.");
        }
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            errorMessage ? *errorMessage : QStringLiteral("metadata commit failed"),
                                            &xml);
        return false;
    }

    if (copy) {
        const QList<QStandardItem*> projects = copy->findItems(m_activeProjectName);
        if (!projects.isEmpty()) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                projects.first(), m_activeDstNode, "SBAS-1.0", FOLDER_ICON);
            if (outputNode) {
                outputNode->setToolTip(m_activeProjectName);
                NodeUtils::findOrCreateChildItem(outputNode, "DeformationRateField", "SBAS",
                                                  finalPaths.first(), IMAGEDATA_ICON);
            }
        }
        emit sendCopy(copy);
    }
    return true;
}

void DeformationRateField_ui::rollbackOutputTransaction(const QString& reason)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, reason);
    m_workerOutputPaths.clear();
}

void DeformationRateField_ui::setControlsEnabled(bool enabled)
{
    ui->comboBox_project->setEnabled(enabled);
    ui->comboBox_srcNode->setEnabled(enabled);
    ui->comboBox_modelType->setEnabled(enabled);
    ui->comboBox_confidenceLevel->setEnabled(enabled);
    ui->lineEdit_cohThreshHigh->setEnabled(enabled);
    ui->lineEdit_cohThreshMid->setEnabled(enabled);
    ui->lineEdit_uncertaintyThreshHigh->setEnabled(enabled);
    ui->lineEdit_uncertaintyThreshMid->setEnabled(enabled);
    ui->comboBox_colorMap->setEnabled(enabled);
    ui->checkBox_showContour->setEnabled(enabled);
    ui->lineEdit_contourInterval->setEnabled(enabled);
    ui->checkBox_showArrow->setEnabled(enabled);
    ui->lineEdit_arrowSpacing->setEnabled(enabled);
    ui->lineEdit_dstNode->setEnabled(enabled);
    ui->buttonBox->buttons().at(0)->setEnabled(enabled);
}

void DeformationRateField_ui::on_buttonBox_accepted()
{
    if (!copy || ui->comboBox_project->count() == 0) return;

    auto items = copy->findItems(ui->comboBox_project->currentText());
    if (items.isEmpty() || !items[0]) return;
    QStandardItem* project_item = items[0];

    if (project_item->rowCount() == 0) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("该工程下未检测到数据！请先进行 SBAS 时序分析或更换工程！"));
        return;
    }

    if (ui->lineEdit_dstNode->text().isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("目标节点名不能为空！"));
        return;
    }

    for (int i = 0; i < project_item->rowCount(); i++) {
        QStandardItem* childItem = project_item->child(i, 0);
        if (childItem && ui->lineEdit_dstNode->text() == childItem->text()) {
            QMessageBox::warning(this, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }

    QStringList filePaths = getSelectedFilePaths();
    if (filePaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("无法获取时序数据 H5 文件路径！"));
        return;
    }

    m_activeProjectName = ui->comboBox_project->currentText();
    QStandardItem* projectPathItem = copy->item(project_item->row(), 1);
    if (!projectPathItem || projectPathItem->text().isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("当前工程路径信息缺失，请重新打开工程！"));
        return;
    }
    m_activeProjectPath = projectPathItem->text();
    m_activeProjectRoot = m_activeProjectPath;
    if (m_activeProjectRoot.endsWith(".insar", Qt::CaseInsensitive)) {
        m_activeProjectRoot = QFileInfo(m_activeProjectRoot).absolutePath();
    }
    m_activeDstNode = ui->lineEdit_dstNode->text().trimmed();
    m_activeModelType = ui->comboBox_modelType->currentData().toInt();
    m_activeInputPaths = filePaths;
    m_activeOutputPaths = QStringList() << QDir(m_activeProjectRoot).absoluteFilePath(
        m_activeDstNode + "/DeformationRateField.h5");
    m_workerOutputPaths.clear();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_activeProjectRoot, m_activeDstNode,
                                            m_activeOutputPaths, m_activeInputPaths,
                                            m_outputTransaction, &transactionError, nullptr,
                                            NodeUtils::getProjectFilePath(this))) {
        QMessageBox::warning(this, "Error", transactionError);
        return;
    }

    m_worker = new DeformationRateFieldWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &DeformationRateField_ui::operate, m_worker, &DeformationRateFieldWorker::analyze_rate_field, Qt::QueuedConnection);
    connect(m_worker, &DeformationRateFieldWorker::updateProcess, this, &DeformationRateField_ui::updateProcess);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    connect(m_worker, &DeformationRateFieldWorker::endProcess, this, &DeformationRateField_ui::endProcess);
    connect(m_worker, &DeformationRateFieldWorker::outputsGenerated,
            this, &DeformationRateField_ui::handleResults);
    connect(m_worker, &DeformationRateFieldWorker::errorProcess, this, [this](QString err) {
        rollbackOutputTransaction(err);
        QMessageBox::warning(this, "Error", err);
        StopThread();
    });
    connect(this, &QWidget::destroyed, this, &DeformationRateField_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &DeformationRateField_ui::StopThread);

    m_thread->start();
    setControlsEnabled(false);

    const QString stagingOutputDir = QDir(m_activeProjectRoot)
        .absoluteFilePath(m_outputTransaction.stagingName);

    emit operate(
        stagingOutputDir,
        m_activeProjectName,
        m_activeDstNode,
        m_activeInputPaths,
        m_activeModelType,
        ui->comboBox_confidenceLevel->currentData().toDouble(),
        ui->lineEdit_cohThreshHigh->text().toDouble(),
        ui->lineEdit_cohThreshMid->text().toDouble(),
        ui->lineEdit_uncertaintyThreshHigh->text().toDouble(),
        ui->lineEdit_uncertaintyThreshMid->text().toDouble(),
        ui->comboBox_colorMap->currentData().toInt(),
        ui->checkBox_showContour->isChecked(),
        ui->lineEdit_contourInterval->text().toInt(),
        ui->checkBox_showArrow->isChecked(),
        ui->lineEdit_arrowSpacing->text().toInt(),
        true
    );
}

void DeformationRateField_ui::on_buttonBox_rejected()
{
    this->close();
}
