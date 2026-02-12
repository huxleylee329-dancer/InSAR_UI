#include"S1_TOPS_BackGeocoding.h"
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
S1_TOPS_BackGeocoding::S1_TOPS_BackGeocoding(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1_TOPS_BackGeocoding)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->checkBox->setChecked(true);
}
S1_TOPS_BackGeocoding::~S1_TOPS_BackGeocoding()
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
    S1_TOPS_BackGeocoding_thread = NULL;
}

void S1_TOPS_BackGeocoding::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void S1_TOPS_BackGeocoding::endProcess()
{
    S1_TOPS_BackGeocoding_thread->thread()->quit();
    S1_TOPS_BackGeocoding_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void S1_TOPS_BackGeocoding::endThread()
{
    S1_TOPS_BackGeocoding_thread->thread()->quit();
    S1_TOPS_BackGeocoding_thread->thread()->wait();
}
void S1_TOPS_BackGeocoding::StopThread()
{
    if (S1_TOPS_BackGeocoding_thread != NULL)
    {
        if (S1_TOPS_BackGeocoding_thread->thread()->isRunning())
        {
            S1_TOPS_BackGeocoding_thread->thread()->requestInterruption();
            S1_TOPS_BackGeocoding_thread->thread()->quit();
            S1_TOPS_BackGeocoding_thread->thread()->wait();
        }
    }

}
void S1_TOPS_BackGeocoding::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void S1_TOPS_BackGeocoding::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->fileedit->setDisabled(0);
        ui->comboBox_3->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->checkBox->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->fileedit->setDisabled(1);
        ui->comboBox_3->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->checkBox->setDisabled(1);
    }


}

void S1_TOPS_BackGeocoding::ShowProjectList(QStandardItemModel* model)
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
                copy->item(i, 0)->setStatusTip(IN_PROCESS);
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

    //初始化图像数据节点
    ui->comboBox_3->clear();
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
        ui->comboBox_3->addItem(pnode->GetText());
        pchild = pchild->NextSiblingElement();
    }
    ui->comboBox_3->setCurrentIndex(0);
}

void S1_TOPS_BackGeocoding::on_comboBox_currentIndexChanged()
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

        //初始化图像数据节点
        ui->comboBox_3->clear();
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
            ui->comboBox_3->addItem(pnode->GetText());
            pchild = pchild->NextSiblingElement();
        }
        ui->comboBox_3->setCurrentIndex(0);
    }
}

void S1_TOPS_BackGeocoding::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
                image_number = project->child(i, 0)->rowCount();
        }

        TiXmlElement* pnode = NULL, * pchild = NULL;
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
        if (ret < 0) return;
        //初始化图像数据节点
        ui->comboBox_3->clear();
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            if (ret == 0 && strcmp(pchild->GetText(), "sentinel") == 0 &&
                QString(pnode->Attribute("name")) == ui->comboBox_2->currentText())
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
            ui->comboBox_3->addItem(pnode->GetText());
            pchild = pchild->NextSiblingElement();
        }
        ui->comboBox_3->setCurrentIndex(0);
    }

}

void S1_TOPS_BackGeocoding::on_comboBox_3_currentIndexChanged()
{

}

void S1_TOPS_BackGeocoding::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据！"));
        return;
    }
    if (ui->fileedit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入存放后向地理编码处理结果文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->fileedit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    int index = ui->comboBox_3->currentIndex() + 1;
    this->image_number = ui->comboBox_3->count();
    if (image_number < 2) return;
    if (!bFlag || index < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("主图像序号应为正整数！"));
        return;
    }
    else if (index > image_number)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("主图像索引超出范围，请确认该数字不超过节点下总图像数量！"));
        return;
    }


    S1_TOPS_BackGeocoding_thread = new MyThread;
    S1_TOPS_BackGeocoding_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_TOPS_BackGeocoding::operate, S1_TOPS_BackGeocoding_thread, &MyThread::S1_TOPS_BackGeocoding, Qt::QueuedConnection);
    connect(S1_TOPS_BackGeocoding_thread, &MyThread::updateProcess, this, &S1_TOPS_BackGeocoding::updateProcess);
    connect(S1_TOPS_BackGeocoding_thread->thread(), &QThread::finished, S1_TOPS_BackGeocoding_thread, &MyThread::deleteLater);
    connect(S1_TOPS_BackGeocoding_thread, &MyThread::endProcess, this, &S1_TOPS_BackGeocoding::endProcess);
    connect(this, &QWidget::destroyed, this, &S1_TOPS_BackGeocoding::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_TOPS_BackGeocoding::StopThread);// , Qt::QueuedConnection);
    connect(S1_TOPS_BackGeocoding_thread, &MyThread::sendModel, this, &S1_TOPS_BackGeocoding::TransitModel);
    S1_TOPS_BackGeocoding_thread->thread()->start();
    ChangeVision(false);
    emit operate(this->image_number, 
        index, this->save_path, 
        ui->comboBox->currentText(), 
        ui->comboBox_2->currentText(),
        ui->fileedit->text(), 
        this->copy,
        ui->checkBox->isChecked()
    );
}

void S1_TOPS_BackGeocoding::on_buttonBox_rejected()
{
    this->close();
}