#include"S1_Deburst.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<FormatConversion.h>
#include "tinyxml.h"
#include "NodeUtils.h"
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
//#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif
S1_Deburst::S1_Deburst(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1Deburst)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    S1_Deburst_worker = nullptr;
    m_thread = nullptr;
}
S1_Deburst::~S1_Deburst()
{
    StopThread();
    if (copy && ui->comboBox->count() > 0)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->itemText(i));
            if (project)
                project->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    S1_Deburst_worker = nullptr;
    m_thread = nullptr;
    delete ui;
    ui = nullptr;
}

void S1_Deburst::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void S1_Deburst::endProcess()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    ui->progressBar->hide();
    this->close();
}
void S1_Deburst::endThread()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}
void S1_Deburst::StopThread()
{
    if (S1_Deburst_worker)
        S1_Deburst_worker->StopProcess();

    QThread* workerThread = m_thread.data();
    if (workerThread && workerThread->isRunning())
    {
        workerThread->requestInterruption();
        workerThread->quit();
        if (QThread::currentThread() != workerThread)
            workerThread->wait();
    }
}
void S1_Deburst::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void S1_Deburst::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->fileedit->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->fileedit->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void S1_Deburst::ShowProjectList(QStandardItemModel* model)
{
    XMLFile xmldoc;
    TiXmlElement* pnode = NULL, * pchild = NULL;
    int ret, count = 0;
    this->copy = model;
    ui->comboBox->clear();
    ui->comboBox_2->clear();
    save_path.clear();
    projectFile.clear();
    if (!copy || copy->rowCount() < 1 || copy->columnCount() < 2)
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("工程数据不完整，请重新打开工程。"));
        return;
    }
    for (int i = 0; i < copy->rowCount(); i++)
    {
        QStandardItem* nameItem = copy->item(i, 0);
        QStandardItem* pathItem = copy->item(i, 1);
        if (!nameItem || !pathItem)
            continue;
        QString tmpProjectFile = pathItem->text() + "/" + nameItem->text();
        ret = xmldoc.XMLFile_load(tmpProjectFile.toStdString().c_str());
        if (ret < 0) continue;
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) continue;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
            if (sensorText && strcmp(sensorText, "sentinel") == 0)
            {
                ui->comboBox->addItem(nameItem->text());
                nameItem->setStatusTip(IN_PROCESS);
                this->save_path = pathItem->text();
                this->projectFile = this->save_path + "/" + nameItem->text();
                ui->comboBox->setCurrentIndex(count++);
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
    }

    if (ui->comboBox->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    //工程文件
    ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
    if (ret < 0) return;
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        ret = xmldoc._find_node(pnode, "Sensor", pchild);
        const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
        const char* nodeName = pnode->Attribute("name");
        if (sensorText && nodeName && strcmp(sensorText, "sentinel") == 0)
        {
            ui->comboBox_2->addItem(nodeName);
        }
        pnode = pnode->NextSiblingElement();
    }
    if (ui->comboBox_2->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);

}

void S1_Deburst::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        
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
        ui->comboBox_2->clear();

        this->projectFile = this->save_path + "/" + project->text();
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
        if (ret < 0) return;
        TiXmlElement* pnode = NULL, * pchild = NULL;
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
            const char* nodeName = pnode->Attribute("name");
            if (sensorText && nodeName && strcmp(sensorText, "sentinel") == 0)
            {
                ui->comboBox_2->addItem(nodeName);
            }
            pnode = pnode->NextSiblingElement();
        }
        if (ui->comboBox_2->count() == 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_2->setCurrentIndex(0);

    }
}

void S1_Deburst::on_comboBox_2_currentIndexChanged()
{

}

void S1_Deburst::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据！"));
        return;
    }
    if (ui->fileedit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入存放burst拼接处理结果文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->fileedit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }


    QStringList inputPaths;
    const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
    if (projects.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到工程节点。"));
        return;
    }
    QStandardItem* sourceNode = nullptr;
    for (int i = 0; i < projects.first()->rowCount(); ++i) {
        QStandardItem* childItem = projects.first()->child(i, 0);
        if (childItem && childItem->text() == ui->comboBox_2->currentText()) {
            sourceNode = childItem;
            break;
        }
    }
    if (!sourceNode) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到输入数据节点。"));
        return;
    }
    for (int i = 0; i < sourceNode->rowCount(); ++i) {
        QStandardItem* pathItem = sourceNode->child(i, 1);
        if (pathItem && !pathItem->text().isEmpty()) {
            inputPaths.append(pathItem->text());
        }
    }
    if (inputPaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("输入影像为空。"));
        return;
    }
    S1_Deburst_worker = new S1DeburstWorker;
    m_thread = new QThread(this);
    S1_Deburst_worker->moveToThread(m_thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_Deburst::operate, S1_Deburst_worker, &S1DeburstWorker::S1_Deburst, Qt::QueuedConnection);
    connect(S1_Deburst_worker, &S1DeburstWorker::updateProcess, this, &S1_Deburst::updateProcess);
    connect(m_thread, &QThread::finished, S1_Deburst_worker, &S1DeburstWorker::deleteLater);
    connect(S1_Deburst_worker, &S1DeburstWorker::endProcess, this, &S1_Deburst::endProcess);
    connect(this, &QWidget::destroyed, this, &S1_Deburst::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_Deburst::StopThread);
    connect(S1_Deburst_worker, &S1DeburstWorker::sendResults, this, &S1_Deburst::handleResults);
    m_thread->start();
    ChangeVision(false);
    emit operate(this->save_path, ui->comboBox->currentText(), ui->fileedit->text(), inputPaths);
}

void S1_Deburst::on_buttonBox_rejected()
{
    this->close();
}

void S1_Deburst::handleResults(
    const QString& dstNode,
    const QStringList& deburstH5Paths,
    const QStringList& originNames)
{
    // Workspace UI 路径的 XML 写入：直接对本地 .insar 文件进行 load/save
    if (save_path.isEmpty() || projectFile.isEmpty())
        return;

    XMLFile xmlfile;
    if (xmlfile.XMLFile_load(projectFile.toStdString().c_str()) < 0)
    {
        return;
    }
    for (int i = 0; i < deburstH5Paths.size() && i < originNames.size(); i++)
    {
        QString relativePath = QString("/%1/%2").arg(dstNode).arg(originNames.at(i) + "_deburst.h5");
        xmlfile.XMLFile_add_S1_Deburst(
            dstNode.toStdString().c_str(),
            (originNames.at(i) + "_deburst").toStdString().c_str(),
            relativePath.toStdString().c_str());
    }
    xmlfile.XMLFile_save(projectFile.toStdString().c_str());
}
