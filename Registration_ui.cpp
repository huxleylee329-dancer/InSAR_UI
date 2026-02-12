#include"Registration_ui.h"
#include"ui_Registration_ui.h"
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
#endif
//#include<FormatConversion.h>
Registration_ui::Registration_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Registration_ui)
{
    ui->setupUi(this);
    Registration_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);
    ui->comboBox_blocksize->addItem("64");
    ui->comboBox_blocksize->addItem("128");
    ui->comboBox_blocksize->addItem("256");
    ui->comboBox_blocksize->addItem("512");
    ui->comboBox_blocksize->setCurrentIndex(2);
    ui->comboBox_interptimes->addItem("2");
    ui->comboBox_interptimes->addItem("4");
    ui->comboBox_interptimes->addItem("8");
    ui->comboBox_interptimes->setCurrentIndex(2);
}
Registration_ui::~Registration_ui()
{
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->comboBox_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    Registration_thread = NULL;
}

void Registration_ui::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    else if (!ui->progressBar_2->isHidden())
    {
        ui->progressBar_2->setValue(value);
        ui->progressBar_2->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar_2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void Registration_ui::endProcess()
{
    if (Registration_thread)
    {
        Registration_thread->thread()->quit();
        Registration_thread->thread()->wait();
        ui->progressBar->hide();
        ui->progressBar_2->hide();
    }
    
    this->close();
}
void Registration_ui::endThread()
{
    Registration_thread->thread()->quit();
    Registration_thread->thread()->wait();
}
void Registration_ui::StopThread()
{
    if (Registration_thread != NULL)
    {
        if (Registration_thread->thread()->isRunning())
        {
            Registration_thread->thread()->requestInterruption();
            Registration_thread->thread()->quit();
            Registration_thread->thread()->wait();
        }
    }
    
}
void Registration_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void Registration_ui::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->fileedit->setDisabled(0);
        ui->comboBox_masterIndex->setDisabled(0);
        ui->comboBox_blocksize->setDisabled(0);
        ui->comboBox_interptimes->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);

        ui->comboBox_project->setDisabled(0);
        ui->comboBox_node->setDisabled(0);
        ui->comboBox_masterIndex2->setDisabled(0);
        ui->lineEdit_dstNode->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->fileedit->setDisabled(1);
        ui->comboBox_masterIndex->setDisabled(1);
        ui->comboBox_blocksize->setDisabled(1);
        ui->comboBox_interptimes->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);

        ui->comboBox_project->setDisabled(1);
        ui->comboBox_node->setDisabled(1);
        ui->comboBox_masterIndex2->setDisabled(1);
        ui->lineEdit_dstNode->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
    }


}

void Registration_ui::ShowProjectList(QStandardItemModel *model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i,0)->text());
        ui->comboBox_project->addItem(model->item(i, 0)->text());
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
            ui->comboBox_project->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    ui->comboBox_node->clear();
    bool isnodefound = false;
    QStandardItem* node = NULL, * imagedata = NULL;
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("complex-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0
            )
        {
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
            ui->comboBox_node->addItem(model->data(model->index(i, 0, pro_index)).toString());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }
        }
        
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);
    ui->comboBox_node->setCurrentIndex(0);

    //初始化图像数据节点
    ui->comboBox_masterIndex->clear();
    ui->comboBox_masterIndex2->clear();
    if (node->rowCount() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("节点无数据！"));
        this->deleteLater();
        return;
    }
    for (int i = 0; i < node->rowCount(); i++)
    {
        ui->comboBox_masterIndex->addItem(node->child(i, 0)->text());
        ui->comboBox_masterIndex2->addItem(node->child(i, 0)->text());
    }
    ui->comboBox_masterIndex->setCurrentIndex(0);
    ui->comboBox_masterIndex2->setCurrentIndex(0);
}
void Registration_ui::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无数据！"));
            this->deleteLater();
            return;
        }
        QStandardItem* node = NULL;
        ui->comboBox_2->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0
                )
            {
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无满足需求的数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_2->setCurrentIndex(0);
        ui->comboBox_masterIndex->clear();
        
        count = node->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            this->deleteLater();
            return;
        }
        for (int i = 0; i < count; i++)
        {
            ui->comboBox_masterIndex->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_masterIndex->setCurrentIndex(0);
    }
}
void Registration_ui::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无数据！"));
            this->deleteLater();
            return;
        }
        QStandardItem* node = NULL;
        ui->comboBox_node->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0
                )
            {
                ui->comboBox_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无满足需求的数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_node->setCurrentIndex(0);
        ui->comboBox_masterIndex2->clear();

        count = node->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            this->deleteLater();
            return;
        }
        for (int i = 0; i < count; i++)
        {
            ui->comboBox_masterIndex2->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_masterIndex2->setCurrentIndex(0);
    }
}


void Registration_ui::on_comboBox_node_currentIndexChanged()
{
    if (ui->comboBox_node->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QString temp = ui->comboBox_node->currentText();
            temp = project->child(i, 0)->text();
            if (project->child(i, 0)->text() == ui->comboBox_node->currentText())
            {
                image_number = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }

        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_masterIndex2->clear();
        int count = node->rowCount();
        for (int i = 0; i < count; i++)
        {
            ui->comboBox_masterIndex2->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_masterIndex2->setCurrentIndex(0);
    }

}

void Registration_ui::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QString temp = ui->comboBox_2->currentText();
            temp = project->child(i, 0)->text();
            if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
            {
                image_number = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }
        
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_masterIndex->clear();
        int count = node->rowCount();
        for (int i = 0; i < count; i++)
        {
            ui->comboBox_masterIndex->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_masterIndex->setCurrentIndex(0);
    }
    
}

void Registration_ui::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据，请先进行裁剪或更换工程！"));
        return;
    }
    if (ui->fileedit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入存放配准文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->fileedit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    int index = ui->comboBox_masterIndex->currentIndex() + 1;
    int interp = ui->comboBox_interptimes->currentText().toInt(&bFlag);
    int block_size = ui->comboBox_blocksize->currentText().toInt(&bFlag);
    Registration_thread = new MyThread;
    Registration_thread->moveToThread(new QThread(this));
    //this->Process->setAutoClose(true);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    QList<int> para;
    para.push_back(index);
    para.push_back(interp);
    para.push_back(block_size);
    para.push_back(this->image_number);
    //this->thread()->msleep(1);
    connect(this, &Registration_ui::operate, Registration_thread, &MyThread::Regis, Qt::QueuedConnection);
    connect(Registration_thread, &MyThread::updateProcess, this, &Registration_ui::updateProcess);
    connect(Registration_thread->thread(), &QThread::finished, Registration_thread, &MyThread::deleteLater);
    connect(Registration_thread, &MyThread::endProcess, this, &Registration_ui::endProcess);
    connect(this, &QWidget::destroyed, this, &Registration_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Registration_ui::StopThread);// , Qt::QueuedConnection);
    connect(Registration_thread, &MyThread::sendModel, this, &Registration_ui::TransitModel);
    Registration_thread->thread()->start();
    ChangeVision(false);
    //connect(thread, &MyThread::endProcess, this, &MainWindow::endProcess);
    emit operate(para, this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(), ui->fileedit->text(), this->copy);
}

void Registration_ui::on_buttonBox_rejected()
{
    this->close();
}

void Registration_ui::on_buttonBox_2_accepted()
{
    if (ui->comboBox_node->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
        return;
    }
    if (ui->lineEdit_dstNode->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入目标节点名！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstNode->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }

    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_project->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstNode->text() == project->child(i)->text() && project->child(i, 1)->text() != "complex-2.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且和配准数据级别不同，请重命名！"));
        return;
    }

    int index = ui->comboBox_masterIndex2->currentIndex() + 1;
    Registration_thread = new MyThread;
    Registration_thread->moveToThread(new QThread(this));
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &Registration_ui::operate2, Registration_thread, &MyThread::DEMAssistCoregistration, Qt::QueuedConnection);
    connect(Registration_thread, &MyThread::updateProcess, this, &Registration_ui::updateProcess);
    connect(Registration_thread->thread(), &QThread::finished, Registration_thread, &MyThread::deleteLater);
    connect(Registration_thread, &MyThread::endProcess, this, &Registration_ui::endProcess);
    connect(this, &QWidget::destroyed, this, &Registration_ui::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &Registration_ui::StopThread);// , Qt::QueuedConnection);
    connect(Registration_thread, &MyThread::sendModel, this, &Registration_ui::TransitModel);
    Registration_thread->thread()->start();
    ChangeVision(false);
    emit operate2(index, this->save_path, ui->comboBox_project->currentText(),
        ui->comboBox_node->currentText(), ui->lineEdit_dstNode->text(), this->copy);
}

void Registration_ui::on_buttonBox_2_rejected()
{
    this->close();
}
