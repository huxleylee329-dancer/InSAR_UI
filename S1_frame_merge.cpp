#include"S1_frame_merge.h"
#include"ui_S1_frame_merge.h"
#include"icon_source.h"
#include"FormatConversion.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#endif
S1_frame_merge::S1_frame_merge(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1_frame_merge)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
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
    S1_frame_merge_thread = NULL;
}

void S1_frame_merge::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void S1_frame_merge::endProcess()
{
    if (S1_frame_merge_thread)
    {
        S1_frame_merge_thread->thread()->quit();
        S1_frame_merge_thread->thread()->wait();
        S1_frame_merge_thread = NULL;
    }
    ui->progressBar->hide();
    this->close();
}
void S1_frame_merge::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (S1_frame_merge_thread)
    {
        S1_frame_merge_thread->thread()->quit();
        S1_frame_merge_thread->thread()->wait();
        S1_frame_merge_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
    //this->close();
}
void S1_frame_merge::endThread()
{
    if (S1_frame_merge_thread)
    {
        S1_frame_merge_thread->thread()->quit();
        S1_frame_merge_thread->thread()->wait();
        S1_frame_merge_thread = NULL;
    }
}
void S1_frame_merge::StopThread()
{
    if (S1_frame_merge_thread != NULL)
    {
        if (S1_frame_merge_thread->thread()->isRunning())
        {
            S1_frame_merge_thread->thread()->requestInterruption();
            S1_frame_merge_thread->thread()->quit();
            S1_frame_merge_thread->thread()->wait();
        }
        S1_frame_merge_thread = NULL;
    }

}
void S1_frame_merge::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
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
    XMLFile xmldoc; QStandardItem* project = NULL;
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无可处理数据，请先导入数据！"));
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
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
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
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
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
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
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstnode->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("名称应当为数字、字母及下划线的组合！"));
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
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且数据等级不符合要求，请重命名！"));
            return;
        }
    }


    S1_frame_merge_thread = new MyThread;
    S1_frame_merge_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_frame_merge::operate, S1_frame_merge_thread, &MyThread::S1_frame_merge, Qt::QueuedConnection);
    connect(S1_frame_merge_thread, &MyThread::updateProcess, this, &S1_frame_merge::updateProcess);
    connect(S1_frame_merge_thread->thread(), &QThread::finished, S1_frame_merge_thread, &MyThread::deleteLater);
    connect(S1_frame_merge_thread, &MyThread::endProcess, this, &S1_frame_merge::endProcess);
    connect(S1_frame_merge_thread, &MyThread::errorProcess, this, &S1_frame_merge::errorProcess);
    connect(this, &QWidget::destroyed, this, &S1_frame_merge::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_frame_merge::StopThread);// , Qt::QueuedConnection);
    connect(S1_frame_merge_thread, &MyThread::sendModel, this, &S1_frame_merge::TransitModel);
    S1_frame_merge_thread->thread()->start();
    ChangeVision(false);
    operate(
        ui->comboBox_data1->currentIndex() + 1,
        ui->comboBox_data2->currentIndex() + 1,
        ui->comboBox_project->currentText(),
        ui->comboBox_node1->currentText(),
        ui->comboBox_node2->currentText(),
        ui->lineEdit_dstnode->text(),
        this->copy
    );
}

void S1_frame_merge::on_buttonBox_rejected()
{
    this->close();
}
