#include"SLC_deramp.h"
#include"ui_SLC_deramp.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<Utils.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
#include"FormatConversion.h"
#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#endif
SLC_deramp::SLC_deramp(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::SLC_deramp)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();

}
SLC_deramp::~SLC_deramp()
{
    if (copy)
    {
        if (copy->findItems(ui->comboBox->currentText())[0])
            copy->findItems(ui->comboBox->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    SLC_deramp_thread = NULL;
}

void SLC_deramp::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void SLC_deramp::endProcess()
{
    SLC_deramp_thread->thread()->quit();
    SLC_deramp_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void SLC_deramp::endThread()
{
    SLC_deramp_thread->thread()->quit();
    SLC_deramp_thread->thread()->wait();
}
void SLC_deramp::StopThread()
{
    if (SLC_deramp_thread != NULL)
        if (SLC_deramp_thread->thread()->isRunning())
        {
            SLC_deramp_thread->thread()->requestInterruption();
            SLC_deramp_thread->thread()->quit();
            SLC_deramp_thread->thread()->wait();
        }

}



void SLC_deramp::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }

    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无可处理数据，请先导入数据！"));
        ui->comboBox_dst_node->clear();
        //ui->comboBox_masterImage->clear();
        return;
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    ui->comboBox_dst_node->clear();
    for (int i = 0; i < count; i++)
    {
        if (project->child(i, 1)->text() == QString("complex-2.0"))
        {
            ui->comboBox_dst_node->addItem(project->child(i, 0)->text());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }

        }
    }
    if (!node)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无数据！"));
        ui->comboBox_dst_node->clear();
        //ui->comboBox_masterImage->clear();
        return;
    }
    ui->comboBox_dst_node->setCurrentIndex(0);

    //ui->comboBox_masterImage->clear();
    //for (int i = 0; i < node->rowCount(); i++)
    //{
    //    ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
    //}
    //if (ui->comboBox_masterImage->count() < 1)
    //{
    //    QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
    //    ui->comboBox_masterImage->clear();
    //    return;
    //}
    //ui->comboBox_masterImage->setCurrentIndex(0);

}
void SLC_deramp::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        bool isnodefound = false;
        QStandardItem* node = NULL;
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        ui->comboBox_dst_node->clear();
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 1)->text() == QString("complex-2.0"))
            {
                ui->comboBox_dst_node->addItem(project->child(i, 0)->text());
                if (!isnodefound)
                {
                    node = project->child(i, 0);
                    isnodefound = true;
                }

            }
        }
        if (!isnodefound)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无数据！"));
            ui->comboBox_dst_node->clear();
            //ui->comboBox_masterImage->clear();
            return;
        }
        //ui->comboBox_masterImage->clear();
        //for (int i = 0; i < node->rowCount(); i++)
        //{
        //    ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
        //}
        //if (ui->comboBox_masterImage->count() < 1)
        //{
        //    QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
        //    ui->comboBox_masterImage->clear();
        //    return;
        //}
        //ui->comboBox_masterImage->setCurrentIndex(0);
    }
}



void SLC_deramp::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_dst_node->setDisabled(0);
        //ui->comboBox_masterImage->setDisabled(0);
        ui->lineEdit->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_dst_node->setDisabled(1);
        //ui->comboBox_masterImage->setDisabled(1);
        ui->lineEdit->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}


void SLC_deramp::on_comboBox_dst_node_currentIndexChanged()
{
   /* if (ui->comboBox_dst_node->count() > 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QString temp = project->child(i, 0)->text();
            if (project->child(i, 0)->text() == ui->comboBox_dst_node->currentText())
            {
                node = project->child(i, 0); break;
            }
        }

        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            ui->comboBox_masterImage->clear();
            return;
        }
        ui->comboBox_masterImage->clear();
        int count = node->rowCount();
        for (int i = 0; i < count; i++)
        {
            ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_masterImage->setCurrentIndex(0);
    }*/
}

void SLC_deramp::on_buttonBox_accepted()
{
    XMLFile xmldoc; 
    TiXmlElement* pnode = NULL, * pchild = NULL;
    bool bFlag = false;
    if (copy->item(ui->comboBox->currentIndex(), 0)->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，请重命名！"));
            return;
        }
    }

    //确定主图像序号
    QString projectFile;
    for (int i = 0; i < copy->rowCount(); i++)
    {
        if (copy->item(i, 0)->text() == ui->comboBox->currentText())
        {
            projectFile = copy->item(i, 1)->text() + "/" + copy->item(i, 0)->text();
            break;
        }
    }
    if (projectFile.isEmpty()) return;
    int ret = xmldoc.XMLFile_load(projectFile.toStdString().c_str());
    if (ret < 0) return;
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        if (pnode->Attribute("name") == ui->comboBox_dst_node->currentText()) break;
        pnode = pnode->NextSiblingElement();
    }
    if (!pnode) return;
    ret = xmldoc._find_node(pnode, "master_image", pchild);
    if (ret < 0) return;
    int index = 1;
    ret = sscanf(pchild->GetText(), "%d", &index);
    //this->image_number = ui->comboBox_masterImage->count();
    SLC_deramp_thread = new MyThread;
    SLC_deramp_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &SLC_deramp::operate, SLC_deramp_thread, &MyThread::SLC_deramp, Qt::QueuedConnection);
    connect(SLC_deramp_thread, &MyThread::updateProcess, this, &SLC_deramp::updateProcess);
    connect(SLC_deramp_thread->thread(), &QThread::finished, SLC_deramp_thread, &MyThread::deleteLater);
    connect(SLC_deramp_thread, &MyThread::endProcess, this, &SLC_deramp::endProcess);
    connect(this, &QWidget::destroyed, this, &SLC_deramp::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SLC_deramp::StopThread);// , Qt::QueuedConnection);
    SLC_deramp_thread->thread()->start();
    ChangeVision(false);
    emit operate(index, ui->comboBox->currentText(), ui->comboBox_dst_node->currentText(), ui->lineEdit->text(), this->copy);

}

void SLC_deramp::on_buttonBox_rejected()
{
    this->close();
}
