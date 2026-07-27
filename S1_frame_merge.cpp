#include"S1_frame_merge.h"
#include"ui_S1FrameMerge.h"
#include"icon_source.h"
#include"FormatConversion.h"
#include "tinyxml.h"
#include"S1FrameMergeWorker.h"
#include "NodeUtils.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QFileInfo>
#include<QDir>
#include<QThread>
#include<QDialogButtonBox>
#include<QRegularExpression>
#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#endif
S1_frame_merge::S1_frame_merge(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1FrameMerge)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    S1_frame_merge_worker = nullptr;
}
S1_frame_merge::~S1_frame_merge()
{
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    S1_frame_merge_worker = nullptr;
}

void S1_frame_merge::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void S1_frame_merge::endProcess()
{
    if (S1_frame_merge_worker)
    {
        S1_frame_merge_worker->thread()->quit();
        S1_frame_merge_worker->thread()->wait();
        S1_frame_merge_worker = nullptr;
    }
    ui->progressBar->hide();
    this->close();
}
void S1_frame_merge::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (S1_frame_merge_worker)
    {
        S1_frame_merge_worker->thread()->quit();
        S1_frame_merge_worker->thread()->wait();
        S1_frame_merge_worker = nullptr;
    }
    ui->progressBar->hide();
    ChangeVision(true);
    //this->close();
}
void S1_frame_merge::endThread()
{
    if (S1_frame_merge_worker)
    {
        S1_frame_merge_worker->thread()->quit();
        S1_frame_merge_worker->thread()->wait();
        S1_frame_merge_worker = nullptr;
    }
}
void S1_frame_merge::StopThread()
{
    if (S1_frame_merge_worker != nullptr)
    {
        if (S1_frame_merge_worker->thread()->isRunning())
        {
            S1_frame_merge_worker->thread()->requestInterruption();
            S1_frame_merge_worker->thread()->quit();
            S1_frame_merge_worker->thread()->wait();
        }
        S1_frame_merge_worker = nullptr;
    }

}
void S1_frame_merge::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project->setDisabled(0);
        ui->comboBox_data1->setDisabled(0);
        ui->comboBox_data2->setDisabled(0);
        ui->comboBox_node1->setDisabled(0);
        ui->comboBox_node2->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->lineEdit_dstnode->setDisabled(0);
    }
    else
    {
        ui->comboBox_project->setDisabled(1);
        ui->comboBox_data1->setDisabled(1);
        ui->comboBox_data2->setDisabled(1);
        ui->comboBox_node1->setDisabled(1);
        ui->comboBox_node2->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->lineEdit_dstnode->setDisabled(1);
    }


}

void S1_frame_merge::ShowProjectList(QStandardItemModel* model)
{
    XMLFile xmldoc;
    QStandardItem* project = NULL;
    TiXmlElement* pnode = NULL, * pchild = NULL;
    int ret, count = 0;
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        QString tmpProjectFile = copy->item(i, 1)->text() + "/" + copy->item(i, 0)->text();
        ret = xmldoc.XMLFile_load(tmpProjectFile.toStdString().c_str());
        if (ret < 0) return;
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0)
            {
                ui->comboBox_project->addItem(copy->item(i, 0)->text());
                copy->item(i, 0)->setStatusTip(IN_PROCESS);
                this->save_path = copy->item(i, 1)->text();
                this->projectFile = this->save_path + "/" + copy->item(i, 0)->text();
                ui->comboBox_project->setCurrentIndex(count++);
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
    }

    if (ui->comboBox_project->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_node1->clear();
    ui->comboBox_node2->clear();
    //工程文件
    ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
    if (ret < 0) return;
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        ret = xmldoc._find_node(pnode, "Sensor", pchild);
        if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0)
        {
            ui->comboBox_node1->addItem(pnode->Attribute("name"));
            ui->comboBox_node2->addItem(pnode->Attribute("name"));
        }
        pnode = pnode->NextSiblingElement();
    }
    if (ui->comboBox_node1->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
        //this->deleteLater();
        return;
    }
    ui->comboBox_node1->setCurrentIndex(0);
    ui->comboBox_node2->setCurrentIndex(0);
    //初始化图像数据节点
    ui->comboBox_data1->clear();
    ui->comboBox_data2->clear();
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        ret = xmldoc._find_node(pnode, "Sensor", pchild);
        if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0)
        {
            break;
        }
        pnode = pnode->NextSiblingElement();
    }
    if (!pnode) return;
    ret = xmldoc._find_node(pnode, "Data", pchild);
    if (ret < 0) return;
    while (pchild)
    {
        if (strcmp(pchild->Value(), "Data") != 0) break;
        ret = xmldoc._find_node(pchild, "Data_Name", pnode); if (ret < 0) return;
        ui->comboBox_data1->addItem(pnode->GetText());
        ui->comboBox_data2->addItem(pnode->GetText());
        pchild = pchild->NextSiblingElement();
    }
    ui->comboBox_data1->setCurrentIndex(0);
    ui->comboBox_data2->setCurrentIndex(0);
}

void S1_frame_merge::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        ui->comboBox_node1->clear();
        ui->comboBox_node2->clear();
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
            if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0)
            {
                ui->comboBox_node1->addItem(pnode->Attribute("name"));
                ui->comboBox_node2->addItem(pnode->Attribute("name"));
            }
            pnode = pnode->NextSiblingElement();
        }
        if (ui->comboBox_node1->count() == 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
            //this->deleteLater();
            return;
        }
        ui->comboBox_node1->setCurrentIndex(0);
        ui->comboBox_node2->setCurrentIndex(0);

        //初始化图像数据节点
        ui->comboBox_data1->clear();
        ui->comboBox_data2->clear();
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0)
            {
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
        if (!pnode) return;
        ret = xmldoc._find_node(pnode, "Data", pchild);
        if (ret < 0) return;
        while (pchild)
        {
            if (strcmp(pchild->Value(), "Data") != 0) break;
            ret = xmldoc._find_node(pchild, "Data_Name", pnode); if (ret < 0) return;
            ui->comboBox_data1->addItem(pnode->GetText());
            ui->comboBox_data2->addItem(pnode->GetText());
            pchild = pchild->NextSiblingElement();
        }
        ui->comboBox_data1->setCurrentIndex(0);
        ui->comboBox_data2->setCurrentIndex(0);
    }
}

void S1_frame_merge::on_comboBox_node1_currentIndexChanged()
{
    if (ui->comboBox_node1->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_node1->currentText())
            {
                num = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }
        ui->comboBox_data1->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            ui->comboBox_data1->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_data1->setCurrentIndex(0);
    }
}

void S1_frame_merge::on_comboBox_node2_currentIndexChanged()
{
    if (ui->comboBox_node2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_node2->currentText())
            {
                num = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }
        ui->comboBox_data2->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            ui->comboBox_data2->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_data2->setCurrentIndex(0);
    }
}

void S1_frame_merge::on_buttonBox_accepted()
{
    if (ui->comboBox_node1->count() == 0 || ui->comboBox_node2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstnode->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("名称应当为数字、字母及下划线的组合！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_project->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstnode->text() == project->child(i)->text() && project->child(i, 1)->text() != QString("complex-0.0"))
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，且数据等级不符合要求，请重命名！"));
            return;
        }
    }


    QStandardItem* firstNode = nullptr;
    QStandardItem* secondNode = nullptr;
    for (int i = 0; i < project->rowCount(); ++i)
    {
        QStandardItem* node = project->child(i, 0);
        if (!node)
            continue;
        if (node->text() == ui->comboBox_node1->currentText())
            firstNode = node;
        if (node->text() == ui->comboBox_node2->currentText())
            secondNode = node;
    }
    const int firstIndex = ui->comboBox_data1->currentIndex();
    const int secondIndex = ui->comboBox_data2->currentIndex();
    if (!firstNode || !secondNode || firstIndex < 0 || secondIndex < 0 ||
        firstNode->rowCount() <= firstIndex || secondNode->rowCount() <= secondIndex)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("Selected input image index is invalid."));
        return;
    }
    QStandardItem* firstPathItem = firstNode->child(firstIndex, 1);
    QStandardItem* secondPathItem = secondNode->child(secondIndex, 1);
    if (!firstPathItem || !secondPathItem)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("Selected input image file was not found."));
        return;
    }
    const QString firstH5Path = firstPathItem->text();
    const QString secondH5Path = secondPathItem->text();
    if (firstH5Path.isEmpty() || secondH5Path.isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("Selected input image file was not found."));
        return;
    }

    S1_frame_merge_worker = new S1FrameMergeWorker();
    QThread* thread = new QThread(this);
    S1_frame_merge_worker->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_frame_merge::operate, S1_frame_merge_worker, &S1FrameMergeWorker::S1_frame_merge, Qt::QueuedConnection);
    connect(S1_frame_merge_worker, &S1FrameMergeWorker::updateProcess, this, &S1_frame_merge::updateProcess);
    connect(thread, &QThread::finished, S1_frame_merge_worker, &S1FrameMergeWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(S1_frame_merge_worker, &S1FrameMergeWorker::endProcess, this, &S1_frame_merge::endProcess);
    connect(S1_frame_merge_worker, &S1FrameMergeWorker::errorProcess, this, &S1_frame_merge::errorProcess);
    connect(this, &QWidget::destroyed, this, &S1_frame_merge::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_frame_merge::StopThread);
    // 接收 sendResult，完成 Workspace UI 路径的 XML 写入
    connect(S1_frame_merge_worker, &S1FrameMergeWorker::sendResult, this, &S1_frame_merge::handleResult);
    thread->start();
    ChangeVision(false);
    operate(
        ui->comboBox_project->currentText(),
        save_path,
        ui->lineEdit_dstnode->text(),
        firstH5Path,
        secondH5Path
    );
}

void S1_frame_merge::on_buttonBox_rejected()
{
    this->close();
}

void S1_frame_merge::handleResult(
    const QString& dstNode,
    const QString& filename,
    const QString& mergedH5Path,
    const QString& savePath,
    const QString& projectName)
{
    // Workspace UI 路径的 XML 写入：直接对本地 .insar 文件操作
    if (savePath.isEmpty() || projectName.isEmpty())
        return;

    QString xml_path = savePath + "/" + projectName;
    QString relative_path = "/" + dstNode + "/" + QFileInfo(mergedH5Path).fileName();

    const QList<QStandardItem*> projects = copy ? copy->findItems(projectName) : QList<QStandardItem*>();
    if (!projects.isEmpty()) {
        QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
            projects.first(), dstNode, "complex-0.0", FOLDER_ICON);
        if (outputNode) {
            outputNode->setToolTip(projectName);
            QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
                outputNode, filename, "complex", mergedH5Path, IMAGEDATA_ICON);
            if (imageItem) {
                outputNode->setChild(imageItem->row(), 1, new QStandardItem(mergedH5Path));
            }
        }
    }

    XMLFile xml;
    if (xml.XMLFile_load(xml_path.toStdString().c_str()) == 0)
    {
        xml.XMLFile_add_origin(
            dstNode.toStdString().c_str(),
            filename.toStdString().c_str(),
            relative_path.toStdString().c_str(),
            "sentinel");
        xml.XMLFile_save(xml_path.toStdString().c_str());
    }
}
