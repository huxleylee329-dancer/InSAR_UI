#include"S1_Deburst.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
//#include<Utils.h>
#include<FormatConversion.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
//#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif
S1_Deburst::S1_Deburst(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1_Deburst)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
}
S1_Deburst::~S1_Deburst()
{
    if (copy && ui->comboBox->count() > 0)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    S1_Deburst_thread = NULL;
}

void S1_Deburst::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void S1_Deburst::endProcess()
{
    S1_Deburst_thread->thread()->quit();
    S1_Deburst_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void S1_Deburst::endThread()
{
    S1_Deburst_thread->thread()->quit();
    S1_Deburst_thread->thread()->wait();
}
void S1_Deburst::StopThread()
{
    if (S1_Deburst_thread != NULL)
    {
        if (S1_Deburst_thread->thread()->isRunning())
        {
            S1_Deburst_thread->thread()->requestInterruption();
            S1_Deburst_thread->thread()->quit();
            S1_Deburst_thread->thread()->wait();
        }
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
                ui->comboBox->addItem(copy->item(i, 0)->text());
                model->item(i, 0)->setStatusTip(IN_PROCESS);
                this->save_path = copy->item(i, 1)->text();
                this->projectFile = this->save_path + "/" + copy->item(i, 0)->text();
                ui->comboBox->setCurrentIndex(count++);
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
    }

    if (ui->comboBox->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->clear();
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
            ui->comboBox_2->addItem(pnode->Attribute("name"));
        }
        pnode = pnode->NextSiblingElement();
    }
    if (ui->comboBox_2->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);

}

void S1_Deburst::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
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
            if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0)
            {
                ui->comboBox_2->addItem(pnode->Attribute("name"));
            }
            pnode = pnode->NextSiblingElement();
        }
        if (ui->comboBox_2->count() == 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据！"));
        return;
    }
    if (ui->fileedit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入存放burst拼接处理结果文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->fileedit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }


    S1_Deburst_thread = new MyThread;
    S1_Deburst_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_Deburst::operate, S1_Deburst_thread, &MyThread::S1_Deburst, Qt::QueuedConnection);
    connect(S1_Deburst_thread, &MyThread::updateProcess, this, &S1_Deburst::updateProcess);
    connect(S1_Deburst_thread->thread(), &QThread::finished, S1_Deburst_thread, &MyThread::deleteLater);
    connect(S1_Deburst_thread, &MyThread::endProcess, this, &S1_Deburst::endProcess);
    connect(this, &QWidget::destroyed, this, &S1_Deburst::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_Deburst::StopThread);// , Qt::QueuedConnection);
    connect(S1_Deburst_thread, &MyThread::sendModel, this, &S1_Deburst::TransitModel);
    S1_Deburst_thread->thread()->start();
    ChangeVision(false);
    emit operate(this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(), ui->fileedit->text(), this->copy);
}

void S1_Deburst::on_buttonBox_rejected()
{
    this->close();
}