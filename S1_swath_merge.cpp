#include"S1_swath_merge.h"
#include"ui_S1_swath_merge.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
S1_swath_merge::S1_swath_merge(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1_swath_merge)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
}
S1_swath_merge::~S1_swath_merge()
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
    S1_swath_merge_thread = NULL;
}

void S1_swath_merge::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void S1_swath_merge::endProcess()
{
    if (S1_swath_merge_thread)
    {
        S1_swath_merge_thread->thread()->quit();
        S1_swath_merge_thread->thread()->wait();
        S1_swath_merge_thread = NULL;
    }
    ui->progressBar->hide();
    this->close();
}
void S1_swath_merge::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (S1_swath_merge_thread)
    {
        S1_swath_merge_thread->thread()->quit();
        S1_swath_merge_thread->thread()->wait();
        S1_swath_merge_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}
void S1_swath_merge::endThread()
{
    if (S1_swath_merge_thread)
    {
        S1_swath_merge_thread->thread()->quit();
        S1_swath_merge_thread->thread()->wait();
        S1_swath_merge_thread = NULL;
    }
}
void S1_swath_merge::StopThread()
{
    if (S1_swath_merge_thread != NULL)
    {
        if (S1_swath_merge_thread->thread()->isRunning())
        {
            S1_swath_merge_thread->thread()->requestInterruption();
            S1_swath_merge_thread->thread()->quit();
            S1_swath_merge_thread->thread()->wait();
        }
        S1_swath_merge_thread = NULL;
    }

}
void S1_swath_merge::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void S1_swath_merge::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project->setDisabled(0);
        ui->comboBox_IW1_node->setDisabled(0);
        ui->comboBox_IW2_node->setDisabled(0);
        ui->comboBox_IW3_node->setDisabled(0);
        ui->comboBox_IW1_data->setDisabled(0);
        ui->comboBox_IW2_data->setDisabled(0);
        ui->comboBox_IW3_data->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->lineEdit_dstNode->setDisabled(0);
    }
    else
    {
        ui->comboBox_project->setDisabled(1);
        ui->comboBox_IW1_node->setDisabled(1);
        ui->comboBox_IW2_node->setDisabled(1);
        ui->comboBox_IW3_node->setDisabled(1);
        ui->comboBox_IW1_data->setDisabled(1);
        ui->comboBox_IW2_data->setDisabled(1);
        ui->comboBox_IW3_data->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->lineEdit_dstNode->setDisabled(1);
    }


}

void S1_swath_merge::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
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
    ui->comboBox_IW1_node->clear();
    ui->comboBox_IW2_node->clear();
    ui->comboBox_IW3_node->clear();
    bool isnodefound = false;
    QStandardItem* node = NULL, * imagedata = NULL;
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0)
        {
            ui->comboBox_IW1_node->addItem(model->data(model->index(i, 0, pro_index)).toString());
            ui->comboBox_IW2_node->addItem(model->data(model->index(i, 0, pro_index)).toString());
            ui->comboBox_IW3_node->addItem(model->data(model->index(i, 0, pro_index)).toString());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }
        }
    }
    if (!isnodefound)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
        //this->deleteLater();
        return;
    }
    ui->comboBox_IW1_node->setCurrentIndex(0);
    ui->comboBox_IW2_node->setCurrentIndex(0);
    ui->comboBox_IW3_node->setCurrentIndex(0);

    ui->comboBox_IW1_data->clear();
    ui->comboBox_IW2_data->clear();
    ui->comboBox_IW3_data->clear();

    //初始化图像数据节点
    if (node->rowCount() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("节点无数据！"));
        //this->deleteLater();
        return;
    }
    for (int i = 0; i < node->rowCount(); i++)
    {
        ui->comboBox_IW1_data->addItem(node->child(i, 0)->text());
        ui->comboBox_IW2_data->addItem(node->child(i, 0)->text());
        ui->comboBox_IW3_data->addItem(node->child(i, 0)->text());
    }
    ui->comboBox_IW1_data->setCurrentIndex(0);
    ui->comboBox_IW2_data->setCurrentIndex(0);
    ui->comboBox_IW3_data->setCurrentIndex(0);
}

void S1_swath_merge::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        if (!project) return;
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无数据！"));
            //this->deleteLater();
            return;
        }
        QStandardItem* node = NULL;
        ui->comboBox_IW1_node->clear();
        ui->comboBox_IW2_node->clear();
        ui->comboBox_IW3_node->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0)
            {
                ui->comboBox_IW1_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                ui->comboBox_IW2_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                ui->comboBox_IW3_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无满足需求的数据！"));
            //this->deleteLater();
            return;
        }
        ui->comboBox_IW1_node->setCurrentIndex(0);
        ui->comboBox_IW2_node->setCurrentIndex(0);
        ui->comboBox_IW3_node->setCurrentIndex(0);

        
        //初始化图像数据节点
        ui->comboBox_IW1_data->clear();
        ui->comboBox_IW2_data->clear();
        ui->comboBox_IW3_data->clear();
        if (node->rowCount() < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < node->rowCount(); i++)
        {
            ui->comboBox_IW1_data->addItem(node->child(i, 0)->text());
            ui->comboBox_IW2_data->addItem(node->child(i, 0)->text());
            ui->comboBox_IW3_data->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_IW1_data->setCurrentIndex(0);
        ui->comboBox_IW2_data->setCurrentIndex(0);
        ui->comboBox_IW3_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_comboBox_IW1_node_currentIndexChanged()
{
    if (ui->comboBox_IW1_node->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_IW1_node->currentText())
            {
                num = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }
        ui->comboBox_IW1_data->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            ui->comboBox_IW1_data->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_IW1_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_comboBox_IW2_node_currentIndexChanged()
{
    if (ui->comboBox_IW2_node->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_IW2_node->currentText())
            {
                num = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }
        ui->comboBox_IW2_data->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            ui->comboBox_IW2_data->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_IW2_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_comboBox_IW3_node_currentIndexChanged()
{
    if (ui->comboBox_IW3_node->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_IW3_node->currentText())
            {
                num = project->child(i, 0)->rowCount(); node = project->child(i, 0); break;
            }
        }
        ui->comboBox_IW3_data->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            ui->comboBox_IW3_data->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_IW3_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_buttonBox_accepted()
{
    if (ui->comboBox_IW1_node->count() == 0 || ui->comboBox_IW2_node->count() == 0 || ui->comboBox_IW3_node->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstNode->text().contains(QRegularExpression("^\\w+$"));
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
        if (ui->lineEdit_dstNode->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，请重命名！"));
            return;
        }
    }


    S1_swath_merge_thread = new MyThread;
    S1_swath_merge_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_swath_merge::operate, S1_swath_merge_thread, &MyThread::S1_swath_merge, Qt::QueuedConnection);
    connect(S1_swath_merge_thread, &MyThread::updateProcess, this, &S1_swath_merge::updateProcess);
    connect(S1_swath_merge_thread->thread(), &QThread::finished, S1_swath_merge_thread, &MyThread::deleteLater);
    connect(S1_swath_merge_thread, &MyThread::endProcess, this, &S1_swath_merge::endProcess);
    connect(S1_swath_merge_thread, &MyThread::errorProcess, this, &S1_swath_merge::errorProcess);
    connect(this, &QWidget::destroyed, this, &S1_swath_merge::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_swath_merge::StopThread);// , Qt::QueuedConnection);
    connect(S1_swath_merge_thread, &MyThread::sendModel, this, &S1_swath_merge::TransitModel);
    S1_swath_merge_thread->thread()->start();
    ChangeVision(false);
    operate(
        ui->comboBox_IW1_data->currentIndex() + 1,
        ui->comboBox_IW2_data->currentIndex() + 1,
        ui->comboBox_IW3_data->currentIndex() + 1,
        ui->comboBox_project->currentText(),
        ui->comboBox_IW1_node->currentText(),
        ui->comboBox_IW2_node->currentText(),
        ui->comboBox_IW3_node->currentText(),
        ui->lineEdit_dstNode->text(),
        this->copy
    );
}

void S1_swath_merge::on_buttonBox_rejected()
{
    this->close();
}
