#include "GacosOnlineService_ui.h"
#include <QThread>
#include <QMessageBox>
#include <QRegularExpression>
#include <QAbstractButton>
#include <QFileInfo>
#include <FormatConversion.h>
#include "icon_source.h"
#include "NodeUtils.h"

GacosOnlineService_ui::GacosOnlineService_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::GacosOnlineService),
    copy(nullptr),
    m_worker(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(true);
}

GacosOnlineService_ui::~GacosOnlineService_ui()
{
    emit sendCopy(copy);
    m_worker = nullptr;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

void GacosOnlineService_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void GacosOnlineService_ui::endProcess()
{
    if (m_worker != nullptr && m_worker->thread()->isRunning())
    {
        m_worker->thread()->quit();
        m_worker->thread()->wait();
    }
    m_worker = nullptr;
    ui->progressBar->hide();
    this->close();
}

void GacosOnlineService_ui::StopThread()
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
}

void GacosOnlineService_ui::ChangeVision(bool Editable)
{
    ui->comboBox->setDisabled(!Editable);
    ui->comboBox_2->setDisabled(!Editable);
    ui->file_name->setDisabled(!Editable);
    ui->lineEdit_email->setDisabled(!Editable);
    ui->lineEdit_apikey->setDisabled(!Editable);
    ui->comboBox_format->setDisabled(!Editable);
    ui->buttonBox->buttons().at(0)->setDisabled(!Editable);
}

void GacosOnlineService_ui::ShowProjectList(QStandardItemModel* model)
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

void GacosOnlineService_ui::on_comboBox_currentIndexChanged()
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

void GacosOnlineService_ui::on_buttonBox_accepted()
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

    // 校验邮箱
    if (ui->lineEdit_email->text().isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("请输入 GACOS 账户邮箱！"));
        return;
    }
    // 校验 API Key
    if (ui->lineEdit_apikey->text().isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("请输入 API Key！"));
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

    const QList<QStandardItem*> projects = copy ? copy->findItems(ui->comboBox->currentText()) : QList<QStandardItem*>();
    if (projects.isEmpty())
        return;
    QStandardItem* inputNode = nullptr;
    for (int i = 0; i < projects.first()->rowCount(); ++i) {
        QStandardItem* node = projects.first()->child(i, 0);
        if (node && node->text() == ui->comboBox_2->currentText()) {
            inputNode = node;
            break;
        }
    }
    QStringList inputPaths;
    if (inputNode) {
        for (int i = 0; i < inputNode->rowCount(); ++i) {
            QStandardItem* nameItem = inputNode->child(i, 0);
            QStandardItem* pathItem = inputNode->child(i, 1);
            if (nameItem && pathItem && nameItem->toolTip() == "phase" && !pathItem->text().isEmpty())
                inputPaths.append(pathItem->text());
        }
    }
    if (inputPaths.isEmpty()) {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("Selected phase files were not found."));
        return;
    }

    m_worker = new GacosOnlineServiceWorker;
    m_worker->moveToThread(new QThread(this));

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &GacosOnlineService_ui::operate, m_worker, &GacosOnlineServiceWorker::doGacosRequest, Qt::QueuedConnection);
    connect(m_worker, &GacosOnlineServiceWorker::updateProcess, this, &GacosOnlineService_ui::updateProcess);
    connect(m_worker, &GacosOnlineServiceWorker::endProcess, this, &GacosOnlineService_ui::endProcess);
    connect(m_worker, &GacosOnlineServiceWorker::outputsGenerated,
            this, &GacosOnlineService_ui::handleResults);
    connect(m_worker->thread(), &QThread::finished, m_worker, &GacosOnlineServiceWorker::deleteLater);
    connect(this, &QWidget::destroyed, this, &GacosOnlineService_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &GacosOnlineService_ui::StopThread);

    m_worker->thread()->start();
    ChangeVision(false);

    emit operate(ui->lineEdit_apikey->text(), ui->lineEdit_email->text(),
                 ui->comboBox_format->currentIndex(),
                 this->save_path, ui->comboBox->currentText(),
                 ui->file_name->text(), inputPaths);
}

void GacosOnlineService_ui::on_buttonBox_rejected()
{
    this->close();
}

void GacosOnlineService_ui::handleResults(
    const QString& dstNode,
    const QStringList& outputNames,
    const QStringList& outputPaths,
    const QString& savePath,
    const QString& projectName)
{
    if (outputNames.size() != outputPaths.size())
        return;

    const QList<QStandardItem*> projects = copy ? copy->findItems(projectName) : QList<QStandardItem*>();
    if (!projects.isEmpty()) {
        QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
            projects.first(), dstNode, "phase-2.5", FOLDER_ICON);
        if (outputNode) {
            outputNode->setToolTip(projectName);
            for (int i = 0; i < outputPaths.size(); ++i) {
                QStandardItem* item = NodeUtils::findOrCreateChildItem(
                    outputNode, outputNames[i], "phase", outputPaths[i], IMAGEDATA_ICON);
                if (item) {
                    outputNode->setChild(item->row(), 1, new QStandardItem(outputPaths[i]));
                }
            }
        }
    }

    XMLFile xml;
    const QString xmlPath = savePath + "/" + projectName;
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) == 0) {
        for (int i = 0; i < outputPaths.size(); ++i) {
            const QString relativePath = QString("/%1/%2").arg(dstNode, QFileInfo(outputPaths[i]).fileName());
            xml.XMLFile_add_unwrap(dstNode.toStdString().c_str(), outputNames[i].toStdString().c_str(),
                relativePath.toStdString().c_str(), 0, 0, "GACOS", 0);
        }
        xml.XMLFile_save(xmlPath.toStdString().c_str());
    }
}
