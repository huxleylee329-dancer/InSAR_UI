#include"Geocoding.h"
#include"ui_Geocoding.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
Geocoding::Geocoding(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Geocoding)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);
    ui->spinBox_multi_az->setValue(1);
    ui->spinBox_multi_az->setMinimum(1);
    ui->spinBox_multi_rg->setValue(1);
    ui->spinBox_multi_rg->setMinimum(1);
}
Geocoding::~Geocoding()
{
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_project1->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project1->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project1->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->comboBox_project2->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project2->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project2->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    Geocoding_thread = NULL;
}

void Geocoding::updateProcess(int value, QString information)
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
void Geocoding::endProcess()
{
    Geocoding_thread->thread()->quit();
    Geocoding_thread->thread()->wait();
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    this->close();
}
void Geocoding::endThread()
{
    Geocoding_thread->thread()->quit();
    Geocoding_thread->thread()->wait();
}
void Geocoding::StopThread()
{
    if (Geocoding_thread != NULL)
    {
        if (Geocoding_thread->thread()->isRunning())
        {
            Geocoding_thread->thread()->requestInterruption();
            Geocoding_thread->thread()->quit();
            Geocoding_thread->thread()->wait();
        }
    }

}
void Geocoding::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void Geocoding::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project1->setDisabled(0);
        ui->comboBox_project2->setDisabled(0);
        ui->comboBox_node1->setDisabled(0);
        ui->comboBox_node2->setDisabled(0);
        ui->lineEdit_dstnode1->setDisabled(0);
        ui->lineEdit_dstnode2->setDisabled(0);
        ui->spinBox_multi_az->setDisabled(0);
        ui->spinBox_multi_rg->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox_project1->setDisabled(1);
        ui->comboBox_project2->setDisabled(1);
        ui->comboBox_node1->setDisabled(1);
        ui->comboBox_node2->setDisabled(1);
        ui->lineEdit_dstnode1->setDisabled(1);
        ui->lineEdit_dstnode2->setDisabled(1);
        ui->spinBox_multi_az->setDisabled(1);
        ui->spinBox_multi_rg->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
    }


}

void Geocoding::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_project1->addItem(model->item(i, 0)->text());
        ui->comboBox_project2->addItem(model->item(i, 0)->text());
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
            ui->comboBox_project1->setCurrentIndex(i);
            ui->comboBox_project2->setCurrentIndex(i);
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
    ui->comboBox_node1->clear();
    ui->comboBox_node2->clear();
    bool isnodefound = false;
    QStandardItem* node = NULL, * imagedata = NULL;
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
            )
        {
            ui->comboBox_node2->addItem(model->data(model->index(i, 0, pro_index)).toString());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }
        }
    }
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("phase-3.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("dem-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("coherence-1.0") == 0
            )
        {
            ui->comboBox_node1->addItem(model->data(model->index(i, 0, pro_index)).toString());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }
        }
    }
    if (ui->comboBox_node1->count() == 0 && ui->comboBox_node2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据！"));
        this->deleteLater();
        return;
    }
    if(ui->comboBox_node1->count() > 0) ui->comboBox_node1->setCurrentIndex(0);
    if (ui->comboBox_node2->count() > 0) ui->comboBox_node2->setCurrentIndex(0);
}

void Geocoding::on_comboBox_project1_currentIndexChanged()
{
    if (ui->comboBox_project1->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project1->currentText())[0];
        if (!project) return;
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
        ui->comboBox_node1->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-3.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("dem-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("coherence-1.0") == 0
                )
            {
                ui->comboBox_node1->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无满足需求的数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_node1->setCurrentIndex(0);
    }
}

void Geocoding::on_comboBox_project2_currentIndexChanged()
{
    if (ui->comboBox_project2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project2->currentText())[0];
        if (!project) return;
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
        ui->comboBox_node2->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
                )
            {
                ui->comboBox_node2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无满足需求的数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_node2->setCurrentIndex(0);
    }
}


void Geocoding::on_buttonBox_accepted()
{
    if (ui->comboBox_node1->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstnode1->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("名称应当为数字、字母及下划线的组合！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_project1->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstnode1->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，请重命名！"));
            return;
        }
    }


    Geocoding_thread = new MyThread;
    Geocoding_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &Geocoding::operate, Geocoding_thread, &MyThread::Geocoding, Qt::QueuedConnection);
    connect(Geocoding_thread, &MyThread::updateProcess, this, &Geocoding::updateProcess);
    connect(Geocoding_thread->thread(), &QThread::finished, Geocoding_thread, &MyThread::deleteLater);
    connect(Geocoding_thread, &MyThread::endProcess, this, &Geocoding::endProcess);
    connect(this, &QWidget::destroyed, this, &Geocoding::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Geocoding::StopThread);// , Qt::QueuedConnection);
    connect(Geocoding_thread, &MyThread::sendModel, this, &Geocoding::TransitModel);
    Geocoding_thread->thread()->start();
    ChangeVision(false);
    operate(1,
        1, 
        1,
        ui->comboBox_project1->currentText(),
        ui->comboBox_node1->currentText(),
        ui->lineEdit_dstnode1->text(), 
        this->copy
    );
}

void Geocoding::on_buttonBox_2_accepted()
{
    if (ui->comboBox_node2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstnode2->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("名称应当为数字、字母及下划线的组合！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_project2->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstnode2->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，请重命名！"));
            return;
        }
    }


    Geocoding_thread = new MyThread;
    Geocoding_thread->moveToThread(new QThread(this));
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &Geocoding::operate, Geocoding_thread, &MyThread::Geocoding, Qt::QueuedConnection);
    connect(Geocoding_thread, &MyThread::updateProcess, this, &Geocoding::updateProcess);
    connect(Geocoding_thread->thread(), &QThread::finished, Geocoding_thread, &MyThread::deleteLater);
    connect(Geocoding_thread, &MyThread::endProcess, this, &Geocoding::endProcess);
    connect(this, &QWidget::destroyed, this, &Geocoding::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &Geocoding::StopThread);// , Qt::QueuedConnection);
    connect(Geocoding_thread, &MyThread::sendModel, this, &Geocoding::TransitModel);
    Geocoding_thread->thread()->start();
    ChangeVision(false);
    operate(
        2,
        ui->spinBox_multi_rg->value(),
        ui->spinBox_multi_az->value(),
        ui->comboBox_project2->currentText(),
        ui->comboBox_node2->currentText(),
        ui->lineEdit_dstnode2->text(),
        this->copy
    );
}

void Geocoding::on_buttonBox_rejected()
{
    this->close();
}

void Geocoding::on_buttonBox_2_rejected()
{
    this->close();
}