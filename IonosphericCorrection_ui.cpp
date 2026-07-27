#include "IonosphericCorrection_ui.h"
#include <QThread>
#include <QMessageBox>
#include <QRegularExpression>
#include <QAbstractButton>
#include <FormatConversion.h>
#include "NodeUtils.h"
#include "Utils.h"
#include "icon_source.h"

IonosphericCorrection_ui::IonosphericCorrection_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::IonosphericCorrection),
    copy(nullptr),
    m_worker(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(true);
}

IonosphericCorrection_ui::~IonosphericCorrection_ui()
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

void IonosphericCorrection_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void IonosphericCorrection_ui::endProcess()
{
    persistGeneratedOutputs();
    cleanUpWorker();
    ui->progressBar->hide();
    this->close();
}

void IonosphericCorrection_ui::StopThread()
{
    if (m_worker && m_worker->thread() && m_worker->thread()->isRunning())
        m_worker->thread()->requestInterruption();
    cleanUpWorker();
}

void IonosphericCorrection_ui::onProcessingError(const QString& error)
{
    cleanUpWorker();
    ui->progressBar->hide();
    ChangeVision(true);
    QMessageBox::warning(this, "Warning!", error);
}

void IonosphericCorrection_ui::onProcessingCancelled()
{
    cleanUpWorker();
    ui->progressBar->hide();
    ChangeVision(true);
}

void IonosphericCorrection_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void IonosphericCorrection_ui::ChangeVision(bool Editable)
{
    ui->comboBox->setDisabled(!Editable);
    ui->comboBox_2->setDisabled(!Editable);
    ui->file_name->setDisabled(!Editable);
    ui->doubleSpinBox_subband->setDisabled(!Editable);
    ui->doubleSpinBox_filter->setDisabled(!Editable);
    ui->checkBox_tec->setDisabled(!Editable);
    ui->buttonBox->buttons().at(0)->setDisabled(!Editable);
}

void IonosphericCorrection_ui::ShowProjectList(QStandardItemModel* model)
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
        // 电离层校正需要已配准的 SLC 数据（complex-2.0 或 complex-3.0）
        if (typeTag == "complex-2.0" || typeTag == "complex-3.0")
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("未检测到可处理数据，请先进行 SLC 配准！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);
}

void IonosphericCorrection_ui::on_comboBox_currentIndexChanged()
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
            if (typeTag == "complex-2.0" || typeTag == "complex-3.0")
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
    }
}

void IonosphericCorrection_ui::on_buttonBox_accepted()
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

    QStringList slcNames, slcPaths;
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
            if (image && path && image->toolTip() == "complex") {
                slcNames.append(image->text());
                slcPaths.append(path->text());
            }
        }
        break;
    }
    if (slcPaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("没有可处理的 SLC 影像！"));
        return;
    }

    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();
    m_worker = new IonosphericCorrectionWorker;
    QThread* workerThread = new QThread(this);
    m_worker->moveToThread(workerThread);

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &IonosphericCorrection_ui::operate, m_worker, &IonosphericCorrectionWorker::doCorrection, Qt::QueuedConnection);
    connect(m_worker, &IonosphericCorrectionWorker::updateProcess, this, &IonosphericCorrection_ui::updateProcess);
    connect(m_worker, &IonosphericCorrectionWorker::outputsGenerated, this,
        [this](const QStringList& outputNames, const QStringList& outputPaths) {
            m_generatedOutputNames = outputNames;
            m_generatedOutputPaths = outputPaths;
        });
    connect(m_worker, &IonosphericCorrectionWorker::endProcess, this, &IonosphericCorrection_ui::endProcess);
    connect(m_worker, &IonosphericCorrectionWorker::errorProcess, this, &IonosphericCorrection_ui::onProcessingError);
    connect(m_worker, &IonosphericCorrectionWorker::cancelled, this, &IonosphericCorrection_ui::onProcessingCancelled);
    connect(workerThread, &QThread::finished, m_worker, &IonosphericCorrectionWorker::deleteLater);
    connect(this, &QWidget::destroyed, this, &IonosphericCorrection_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &IonosphericCorrection_ui::StopThread);

    workerThread->start();
    ChangeVision(false);

    emit operate(ui->doubleSpinBox_subband->value(),
                 ui->doubleSpinBox_filter->value(),
                 ui->checkBox_tec->isChecked(),
                 this->save_path, ui->comboBox->currentText(),
                 ui->comboBox_2->currentText(), ui->file_name->text(),
                  slcNames, slcPaths);
}

void IonosphericCorrection_ui::cleanUpWorker()
{
    if (!m_worker)
        return;

    QThread* workerThread = m_worker->thread();
    if (workerThread && workerThread->isRunning()) {
        workerThread->quit();
        workerThread->wait();
    }
    if (workerThread)
        workerThread->deleteLater();
    m_worker = nullptr;
}

void IonosphericCorrection_ui::persistGeneratedOutputs()
{
    if (!copy || m_generatedOutputNames.size() != m_generatedOutputPaths.size() ||
        m_generatedOutputPaths.isEmpty()) {
        return;
    }

    const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
    if (projects.isEmpty())
        return;

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), ui->file_name->text(), "complex-1.5", FOLDER_ICON);
    if (!outputNode)
        return;

    XMLFile xml;
    const QString xmlPath = save_path + "/" + ui->comboBox->currentText() + ".Insar";
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("输出已生成，但项目 XML 保存失败。"));
        return;
    }

    for (int i = 0; i < m_generatedOutputPaths.size(); ++i) {
        const QString relativePath = QString("/%1/%2.h5")
            .arg(ui->file_name->text(), m_generatedOutputNames[i]);
        NodeUtils::findOrCreateChildItem(outputNode, m_generatedOutputNames[i], "complex",
            m_generatedOutputPaths[i], IMAGEDATA_ICON);
        xml.XMLFile_add_unwrap(ui->file_name->text().toStdString().c_str(),
            m_generatedOutputNames[i].toStdString().c_str(), relativePath.toStdString().c_str(),
            0, 0, "Ionospheric_Correction", 0);
    }
    xml.XMLFile_save(xmlPath.toStdString().c_str());
    emit sendCopy(copy);
}

void IonosphericCorrection_ui::on_buttonBox_rejected()
{
    this->close();
}
