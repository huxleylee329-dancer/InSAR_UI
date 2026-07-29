#include "PhaseElevationRegression_ui.h"
#include <QThread>
#include <QMessageBox>
#include <QRegularExpression>
#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
#include <FormatConversion.h>
#include "NodeUtils.h"
#include "icon_source.h"
#include "tinyxml.h"

PhaseElevationRegression_ui::PhaseElevationRegression_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PhaseElevationRegression),
    copy(nullptr),
    m_worker(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(true);
}

PhaseElevationRegression_ui::~PhaseElevationRegression_ui()
{
    StopThread();
    emit sendCopy(copy);
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

void PhaseElevationRegression_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void PhaseElevationRegression_ui::endProcess()
{
    QString error;
    const bool committed = persistGeneratedOutputs(&error);
    if (m_worker != nullptr && m_worker->thread()->isRunning())
    {
        m_worker->thread()->quit();
        m_worker->thread()->wait();
    }
    m_worker = nullptr;
    ui->progressBar->hide();
    if (!committed) {
        QMessageBox::warning(this, "Warning!", error.isEmpty()
            ? QStringLiteral("回归校正输出事务提交失败。") : error);
        ChangeVision(true);
        return;
    }
    this->close();
}

void PhaseElevationRegression_ui::StopThread()
{
    if (m_worker != nullptr)
    {
        if (m_worker->thread()->isRunning())
        {
            m_worker->thread()->requestInterruption();
            m_worker->thread()->quit();
            m_worker->thread()->wait();
        }
        m_worker = nullptr;
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"));
    ui->progressBar->hide();
    ChangeVision(true);
}

void PhaseElevationRegression_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void PhaseElevationRegression_ui::ChangeVision(bool Editable)
{
    ui->comboBox->setDisabled(!Editable);
    ui->comboBox_2->setDisabled(!Editable);
    ui->file_name->setDisabled(!Editable);
    ui->comboBox_method->setDisabled(!Editable);
    ui->lineEdit_window->setDisabled(!Editable);
    ui->doubleSpinBox_coherence->setDisabled(!Editable);
    ui->buttonBox->buttons().at(0)->setDisabled(!Editable);
}

void PhaseElevationRegression_ui::ShowProjectList(QStandardItemModel* model)
{
    if (!model) return;
    if (model->rowCount() < 1) return;
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = nullptr;
    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        if (model->item(i, 0)->rowCount() != 0)
        {
            count = model->item(i, 0)->rowCount();
            project = model->item(i, 0);
            ui->comboBox->setCurrentIndex(i);
            break;
        }
    }
    if (count == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    for (int i = 0; i < count; i++)
    {
        QString typeTag = model->data(model->index(i, 1, pro_index)).toString();
        if (typeTag.startsWith("phase"))
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("未检测到可处理数据，请先生成干涉相位！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);
}

void PhaseElevationRegression_ui::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_2->clear();
        for (int i = 0; i < count; i++)
        {
            QString typeTag = copy->data(copy->index(i, 1, pro_index)).toString();
            if (typeTag.startsWith("phase"))
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
    }
}

void PhaseElevationRegression_ui::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("当前无可用数据节点！"));
        return;
    }

    if (m_worker != nullptr)
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("任务正在执行中，请勿重复操作！"));
        return;
    }

    // 校验滑动窗口输入
    bool ok = false;
    int windowSize = ui->lineEdit_window->text().toInt(&ok);
    if (!ok || windowSize < 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("滑动窗口尺寸应为非负整数！"));
        return;
    }

    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("请输入目标文件夹名称！"));
        return;
    }
    if (!ui->file_name->text().contains(QRegularExpression("^\\w+$")))
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }

    QStringList phaseNames;
    QStringList phasePaths;
    if (!copy) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("项目模型不可用。"));
        return;
    }
    const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
    if (projects.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程。"));
        return;
    }
    QStandardItem* project = projects.first();
    for (int i = 0; i < project->rowCount(); ++i) {
        QStandardItem* node = project->child(i, 0);
        if (!node || node->text() != ui->comboBox_2->currentText())
            continue;
        for (int j = 0; j < node->rowCount(); ++j) {
            QStandardItem* image = node->child(j, 0);
            QStandardItem* path = node->child(j, 1);
            if (image && path && image->toolTip() == "phase") {
                phaseNames.append(image->text());
                phasePaths.append(path->text());
            }
        }
        break;
    }
    if (phasePaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("没有可校正的干涉图。"));
        return;
    }

    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();
    m_generatedOffsetRows.clear();
    m_generatedOffsetCols.clear();
    m_preparedOutputPaths.clear();

    QString projectRoot = save_path;
    if (projectRoot.endsWith(".insar", Qt::CaseInsensitive)) {
        projectRoot = QFileInfo(projectRoot).absolutePath();
    }
    const QString outputNodeName = ui->file_name->text();
    for (const QString& phaseName : phaseNames) {
        m_preparedOutputPaths.append(QDir(projectRoot).absoluteFilePath(
            outputNodeName + "/" + phaseName + "_atmos.h5"));
    }
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(projectRoot, outputNodeName, m_preparedOutputPaths,
                                           phasePaths, m_outputTransaction, &transactionError)) {
        QMessageBox::warning(this, "Warning!", transactionError);
        return;
    }

    m_worker = new PhaseElevationRegressionWorker;
    m_worker->moveToThread(new QThread(this));

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &PhaseElevationRegression_ui::operate, m_worker,
            &PhaseElevationRegressionWorker::doRegression, Qt::QueuedConnection);
    connect(m_worker, &PhaseElevationRegressionWorker::updateProcess, this, &PhaseElevationRegression_ui::updateProcess);
    connect(m_worker, &PhaseElevationRegressionWorker::outputsGenerated, this,
        [this](const QStringList& names, const QStringList& paths,
               const QList<int>& rows, const QList<int>& cols) {
            m_generatedOutputNames = names;
            m_generatedOutputPaths = paths;
            m_generatedOffsetRows = rows;
            m_generatedOffsetCols = cols;
        });
    connect(m_worker, &PhaseElevationRegressionWorker::endProcess, this, &PhaseElevationRegression_ui::endProcess);
    connect(m_worker, &PhaseElevationRegressionWorker::errorProcess, this, &PhaseElevationRegression_ui::onWorkerError);
    connect(m_worker, &PhaseElevationRegressionWorker::cancelled, this, &PhaseElevationRegression_ui::StopThread);
    connect(m_worker->thread(), &QThread::finished, m_worker, &PhaseElevationRegressionWorker::deleteLater);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &PhaseElevationRegression_ui::StopThread);

    m_worker->thread()->start();
    ChangeVision(false);

    int polyOrder = ui->comboBox_method->currentIndex() + 1; // 0→1(线性), 1→2(二次)
    double coherenceThresh = ui->doubleSpinBox_coherence->value();

    emit operate(polyOrder, windowSize, coherenceThresh,
                 QDir(projectRoot).absoluteFilePath(m_outputTransaction.stagingName),
                 phaseNames, phasePaths);
}

bool PhaseElevationRegression_ui::persistGeneratedOutputs(QString* errorMessage)
{
    if (!copy || m_generatedOutputNames.size() != m_generatedOutputPaths.size() ||
        m_generatedOutputPaths.size() != m_generatedOffsetRows.size() ||
        m_generatedOutputPaths.size() != m_generatedOffsetCols.size() ||
        m_generatedOutputPaths.size() != m_preparedOutputPaths.size() ||
        m_generatedOutputPaths.isEmpty()) {
        const QString reason = QStringLiteral("回归校正未生成完整输出结果。");
        if (errorMessage) *errorMessage = reason;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason);
        return false;
    }

    for (int i = 0; i < m_preparedOutputPaths.size(); ++i) {
        if (QFileInfo(m_generatedOutputPaths[i]).fileName() != QFileInfo(m_preparedOutputPaths[i]).fileName() ||
            m_generatedOutputNames[i] != QFileInfo(m_preparedOutputPaths[i]).completeBaseName()) {
            const QString reason = QStringLiteral("回归校正输出名称与事务清单不一致。");
            if (errorMessage) *errorMessage = reason;
            NodeUtils::abandonOutputTransaction(m_outputTransaction, reason);
            return false;
        }
    }

    const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
    if (projects.isEmpty()) {
        const QString reason = QStringLiteral("未找到当前工程项目树。");
        if (errorMessage) *errorMessage = reason;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason);
        return false;
    }

    QString projectRoot = save_path;
    if (projectRoot.endsWith(".insar", Qt::CaseInsensitive)) {
        projectRoot = QFileInfo(projectRoot).absolutePath();
    }
    QString projectFileName = ui->comboBox->currentText();
    if (!projectFileName.endsWith(".insar", Qt::CaseInsensitive)) {
        projectFileName += QStringLiteral(".Insar");
    }
    const QString xmlPath = QDir(projectRoot).absoluteFilePath(projectFileName);
    QString transactionError;
    QStringList finalPaths;
    XMLFile xml;
    if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0 ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, &xml, xmlPath, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("回归校正输出事务校验失败。") : transactionError;
        if (errorMessage) *errorMessage = reason;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        return false;
    }

    const QString outputNodeName = ui->file_name->text();
    TiXmlElement* root = nullptr;
    if (xml.get_root(root) < 0 || !root) {
        const QString reason = QStringLiteral("无法读取项目 XML 根节点。");
        if (errorMessage) *errorMessage = reason;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        return false;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && outputNodeName == QString::fromLocal8Bit(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    for (int i = 0; i < finalPaths.size(); ++i) {
        const QString relativePath = QString("/%1/%2")
            .arg(outputNodeName, QFileInfo(finalPaths[i]).fileName());
        xml.XMLFile_add_unwrap(ui->file_name->text().toStdString().c_str(),
            m_generatedOutputNames[i].toStdString().c_str(), relativePath.toStdString().c_str(),
            m_generatedOffsetRows[i], m_generatedOffsetCols[i], "PhaseElevationRegression", 0);
    }
    if (!NodeUtils::saveProjectXmlAtomically(&xml, xmlPath, &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        const QString reason = transactionError.isEmpty()
            ? QStringLiteral("回归校正输出元数据提交失败。") : transactionError;
        if (errorMessage) *errorMessage = reason;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, &xml);
        return false;
    }

    QStandardItem* projectItem = projects.first();
    for (int row = projectItem->rowCount() - 1; row >= 0; --row) {
        QStandardItem* nodeItem = projectItem->child(row, 0);
        if (nodeItem && nodeItem->text() == outputNodeName) projectItem->removeRow(row);
    }
    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projectItem, outputNodeName, "phase-2.5", FOLDER_ICON);
    for (int i = 0; i < finalPaths.size(); ++i) {
        NodeUtils::findOrCreateChildItem(outputNode, m_generatedOutputNames[i], "phase",
            finalPaths[i], IMAGEDATA_ICON);
    }
    emit sendCopy(copy);
    return true;
}

void PhaseElevationRegression_ui::onWorkerError(const QString& error)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error);
    if (m_worker && m_worker->thread()->isRunning()) {
        m_worker->thread()->quit();
        m_worker->thread()->wait();
    }
    m_worker = nullptr;
    ui->progressBar->hide();
    ChangeVision(true);
    QMessageBox::warning(this, "Warning!", error);
}

void PhaseElevationRegression_ui::on_buttonBox_rejected()
{
    this->close();
}
