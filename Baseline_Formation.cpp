#include<QThread>
#include<QFile>
#include<QDir>
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<Utils.h>
#include"Baseline_Formation.h"
#include"ui_BaselineFormation.h"
#include"icon_source.h"
#include"Coordinate.h"
#include "NodeUtils.h"
Baseline_Formation::Baseline_Formation(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::BaselineFormation)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();
    temporal_thresh = 100.0; spatial_thresh = 100000; temporal_thresh_low = 0.0;

}
Baseline_Formation::~Baseline_Formation()
{
    StopThread();
    if (copy)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
        if (project)
            project->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    Baseline_Formation_thread = NULL;
    delete ui;
    ui = nullptr;
}

void Baseline_Formation::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
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
    BaselineWorker* worker = Baseline_Formation_thread.data();
    if (!worker) return;

    QThread* workerThread = worker->thread();
    worker->StopProcess();
    if (workerThread && workerThread->isRunning())
    {
        workerThread->requestInterruption();
        workerThread->quit();
        if (QThread::currentThread() != workerThread)
            workerThread->wait();
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
    if (!copy || copy->rowCount() < 1 || copy->columnCount() < 2 ||
        !copy->item(0, 0) || !copy->item(0, 1))
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("当前没有可用工程，请先新建或打开工程。"));
        return;
    }
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (!projectItem) continue;
        ui->comboBox->addItem(projectItem->text());
        projectItem->setStatusTip(IN_PROCESS);
    }

    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;
    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (projectItem && projectItem->rowCount() != 0)
        {
            count = projectItem->rowCount();
            project = projectItem;
            ui->comboBox->setCurrentIndex(ui->comboBox->findText(projectItem->text()));
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        ui->comboBox_dst_node->clear();
        ui->comboBox_masterImage->clear();
        return;
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    ui->comboBox_dst_node->clear();
    for (int i = 0; i < count; i++)
    {
        QStandardItem* typeItem = project->child(i, 1);
        QStandardItem* nameItem = project->child(i, 0);
        if (typeItem && nameItem &&
            (typeItem->text() == QString("complex-2.0") ||
             typeItem->text() == QString("complex-3.0"))
            )
        {
            ui->comboBox_dst_node->addItem(nameItem->text());
            if (!isnodefound)
            {
                node = nameItem;
                isnodefound = true;
            }

        }
    }
    if (!node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
        ui->comboBox_dst_node->clear();
        ui->comboBox_masterImage->clear();
        return;
    }
    ui->comboBox_dst_node->setCurrentIndex(0);

    ui->comboBox_masterImage->clear();
    for (int i = 0; i < node->rowCount(); i++)
    {
        QStandardItem* imageItem = node->child(i, 0);
        if (imageItem)
            ui->comboBox_masterImage->addItem(imageItem->text());
    }
    if (ui->comboBox_masterImage->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
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
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
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
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
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
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
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
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
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
    QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
    if (!project)
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
        return;
    }
    if (project->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    int ret;
    ret = sscanf(ui->lineEdit_spatial_thresh->text().toStdString().c_str(), "%lf", &spatial_thresh);
    if (ret != 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入合法空间基线阈值！"));
        return;
    }
    ret = sscanf(ui->lineEdit_temporal_thresh->text().toStdString().c_str(), "%lf", &temporal_thresh);
    if (ret != 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入合法时间基线阈值！"));
        return;
    }
    ret = sscanf(ui->lineEdit_temporal_thresh_low->text().toStdString().c_str(), "%lf", &temporal_thresh_low);
    if (ret != 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入合法时间基线阈值！"));
        return;
    }
    int index = ui->comboBox_masterImage->currentIndex() + 1;
    this->image_number = ui->comboBox_masterImage->count();

    QStandardItem* image = NULL;
    for (int i = 0; i < project->rowCount(); i++) {
        if (project->child(i, 0)->text() == ui->comboBox_dst_node->currentText()) {
            image = project->child(i, 0);
            break;
        }
    }
    if (!image || image->rowCount() == 0)
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选节点不存在或没有可处理数据，请重新选择。"));
        return;
    }
    QStringList filePaths;
    for (int i = 0; i < image->rowCount(); i++) {
        filePaths.append(image->child(i, 1)->text());
    }

    Baseline_Formation_thread = new BaselineWorker();
    QThread* thread = new QThread(this);
    Baseline_Formation_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    
    connect(this, &Baseline_Formation::operate, Baseline_Formation_thread, &BaselineWorker::Baseline_Estimate, Qt::QueuedConnection);
    connect(Baseline_Formation_thread, &BaselineWorker::updateProcess, this, &Baseline_Formation::updateProcess);
    connect(thread, &QThread::finished, Baseline_Formation_thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(Baseline_Formation_thread, &BaselineWorker::sendBL, this, &Baseline_Formation::Paint_Baseline);
    connect(Baseline_Formation_thread, &BaselineWorker::endProcess, this, &Baseline_Formation::endProcess);
    connect(Baseline_Formation_thread, &BaselineWorker::errorProcess, this, [this](QString err) {
        QMessageBox::warning(this, "Error", err);
        StopThread();
    });
    connect(this, &QWidget::destroyed, this, &Baseline_Formation::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Baseline_Formation::StopThread);
    
    thread->start();
    ChangeVision(false);
    emit operate(index, filePaths);

}

void Baseline_Formation::on_buttonBox_rejected()
{
    this->close();
}
