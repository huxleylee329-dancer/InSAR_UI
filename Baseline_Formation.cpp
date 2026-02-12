#include"Baseline_Formation.h"
#include"ui_Baseline_Formation.h"
#include"Coordinate.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<Utils.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
Baseline_Formation::Baseline_Formation(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Baseline_Formation)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();
    temporal_thresh = 100.0; spatial_thresh = 100000; temporal_thresh_low = 0.0;

}
Baseline_Formation::~Baseline_Formation()
{
    if (copy)
    {
        if (copy->findItems(ui->comboBox->currentText())[0])
            copy->findItems(ui->comboBox->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    Baseline_Formation_thread = NULL;
}

void Baseline_Formation::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Baseline_Formation::endProcess()
{
    Baseline_Formation_thread->thread()->quit();
    Baseline_Formation_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void Baseline_Formation::endThread()
{
    Baseline_Formation_thread->thread()->quit();
    Baseline_Formation_thread->thread()->wait();
}
void Baseline_Formation::StopThread()
{
    if (Baseline_Formation_thread != NULL)
        if (Baseline_Formation_thread->thread()->isRunning())
        {
            Baseline_Formation_thread->thread()->requestInterruption();
            Baseline_Formation_thread->thread()->quit();
            Baseline_Formation_thread->thread()->wait();
        }

}
void Baseline_Formation::Paint_Baseline(
    QList<double> temporal_baseline,
    QList<double> spatial_baseline,
    int index
)
{
    if (temporal_baseline.size() < 2 || spatial_baseline.size() < 2) return;
    Coordinate* map = new Coordinate();
    map->show();
    map->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(this, &Baseline_Formation::sendBaseline, map, &Coordinate::Paint2);
    emit sendBaseline(temporal_baseline, spatial_baseline, index, temporal_thresh, temporal_thresh_low, spatial_thresh);
}


void Baseline_Formation::ShowProjectList(QStandardItemModel* model)
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
        ui->comboBox_masterImage->clear();
        return;
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    ui->comboBox_dst_node->clear();
    for (int i = 0; i < count; i++)
    {
        if (project->child(i, 1)->text() == QString("complex-2.0") ||
            project->child(i, 1)->text() == QString("complex-3.0")
            )
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
        ui->comboBox_masterImage->clear();
        return;
    }
    ui->comboBox_dst_node->setCurrentIndex(0);

    ui->comboBox_masterImage->clear();
    for (int i = 0; i < node->rowCount(); i++)
    {
        ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
    }
    if (ui->comboBox_masterImage->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
        ui->comboBox_masterImage->clear();
        return;
    }
    ui->comboBox_masterImage->setCurrentIndex(0);

}
void Baseline_Formation::on_comboBox_currentIndexChanged()
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
            if (project->child(i, 1)->text() == QString("complex-2.0") ||
                project->child(i, 1)->text() == QString("complex-3.0")
                )
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
            ui->comboBox_masterImage->clear();
            return;
        }
        for (int i = 0; i < node->rowCount(); i++)
        {
            ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
        }
        if (ui->comboBox_masterImage->count() < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点无数据！"));
            ui->comboBox_masterImage->clear();
            return;
        }
        ui->comboBox_masterImage->setCurrentIndex(0);
    }
}



void Baseline_Formation::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_dst_node->setDisabled(0);
        ui->comboBox_masterImage->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->lineEdit_spatial_thresh->setDisabled(0);
        ui->lineEdit_temporal_thresh->setDisabled(0);
        ui->lineEdit_temporal_thresh_low->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_dst_node->setDisabled(1);
        ui->comboBox_masterImage->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->lineEdit_spatial_thresh->setDisabled(1);
        ui->lineEdit_temporal_thresh->setDisabled(1);
        ui->lineEdit_temporal_thresh_low->setDisabled(1);
    }


}


void Baseline_Formation::on_comboBox_dst_node_currentIndexChanged()
{
    if (ui->comboBox_dst_node->count() > 0)
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
    }
}

void Baseline_Formation::on_buttonBox_accepted()
{
    bool bFlag = false;
    if (copy->item(ui->comboBox->currentIndex(), 0)->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    int ret;
    ret = sscanf(ui->lineEdit_spatial_thresh->text().toStdString().c_str(), "%lf", &spatial_thresh);
    if (ret != 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入合法空间基线阈值！"));
        return;
    }
    ret = sscanf(ui->lineEdit_temporal_thresh->text().toStdString().c_str(), "%lf", &temporal_thresh);
    if (ret != 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入合法时间基线阈值！"));
        return;
    }
    ret = sscanf(ui->lineEdit_temporal_thresh_low->text().toStdString().c_str(), "%lf", &temporal_thresh_low);
    if (ret != 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入合法时间基线阈值！"));
        return;
    }
    int index = ui->comboBox_masterImage->currentIndex() + 1;/*= ui->Index_edit->text().toUInt(&bFlag)*/;
    this->image_number = ui->comboBox_masterImage->count();
    Baseline_Formation_thread = new MyThread;
    Baseline_Formation_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &Baseline_Formation::operate, Baseline_Formation_thread, &MyThread::Baseline_Formation, Qt::QueuedConnection);
    connect(Baseline_Formation_thread, &MyThread::updateProcess, this, &Baseline_Formation::updateProcess);
    connect(Baseline_Formation_thread->thread(), &QThread::finished, Baseline_Formation_thread, &MyThread::deleteLater);
    connect(Baseline_Formation_thread, &MyThread::sendBL, this, &Baseline_Formation::Paint_Baseline);
    connect(Baseline_Formation_thread, &MyThread::endProcess, this, &Baseline_Formation::endProcess);
    connect(this, &QWidget::destroyed, this, &Baseline_Formation::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Baseline_Formation::StopThread);// , Qt::QueuedConnection);
    Baseline_Formation_thread->thread()->start();
    ChangeVision(false);
    emit operate(index, ui->comboBox->currentText(), ui->comboBox_dst_node->currentText(), this->copy);

}

void Baseline_Formation::on_buttonBox_rejected()
{
    this->close();
}
