#include <complex>
#include <QFile>
#include <QDir>
#include <qdialog.h>
#include <qcheckbox.h>
#include <qscrollarea.h>
#include <qmessagebox.h>
#include <Utils.h>
#include "Baseline.h"
#include "ui_Baseline.h"
#include "Coordinate.h"
#include "icon_source.h"
#include "NodeUtils.h"

Baseline::Baseline(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Baseline)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();
    
}
Baseline::~Baseline()
{
    StopThread();
    if (copy)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
        if (project)
            project->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    m_worker = nullptr;
    m_thread = nullptr;
    delete ui;
    ui = nullptr;
}

void Baseline::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Baseline::endProcess()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    ui->progressBar->hide();
    this->close();
}
void Baseline::endThread()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}
void Baseline::StopThread()
{
    if (m_worker)
        m_worker->StopProcess();

    QThread* workerThread = m_thread.data();
    if (workerThread && workerThread->isRunning())
    {
        workerThread->requestInterruption();
        workerThread->quit();
        if (QThread::currentThread() != workerThread)
            workerThread->wait();
    }
}
void Baseline::Paint_Baseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index)
{
    if (temporal_baseline.size() < 2 || spatial_baseline.size() < 2) return;
    //Coordinate* map = new Coordinate();
    Baseline_Preview* map = new Baseline_Preview();
    map->show();
    map->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(this, &Baseline::sendBaseline, map, &Baseline_Preview::Paint);
    emit sendBaseline(temporal_baseline, spatial_baseline, index);
}


void Baseline::ShowProjectList(QStandardItemModel* model)
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
            (typeItem->text() == QString("complex-0.0") ||
             typeItem->text() == QString("complex-1.0") ||
             typeItem->text() == QString("complex-2.0") ||
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
void Baseline::on_comboBox_currentIndexChanged()
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
            if (project->child(i, 1)->text() == QString("complex-0.0") ||
                project->child(i, 1)->text() == QString("complex-1.0") ||
                project->child(i, 1)->text() == QString("complex-2.0") ||
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



void Baseline::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_dst_node->setDisabled(0);
        ui->comboBox_masterImage->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_dst_node->setDisabled(1);
        ui->comboBox_masterImage->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}


void Baseline::on_comboBox_dst_node_currentIndexChanged()
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

void Baseline::on_buttonBox_accepted()
{
    QString project_name = ui->comboBox->currentText();
    QStandardItem* project = NodeUtils::findFirstModelItem(copy, project_name);
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
    int index = ui->comboBox_masterImage->currentIndex() + 1;
    this->image_number = ui->comboBox_masterImage->count();

    // Prepare file paths list from the model
    QString dst_node = ui->comboBox_dst_node->currentText();
    QStandardItem* node = nullptr;
    if (project) {
        for (int i = 0; i < project->rowCount(); i++) {
            if (project->child(i, 0)->text() == dst_node) {
                node = project->child(i, 0);
                break;
            }
        }
    }
    if (!node || node->rowCount() == 0) {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
        return;
    }
    QStringList filePaths;
    for (int i = 0; i < node->rowCount(); i++) {
        filePaths.append(node->child(i, 1)->text());
    }

    m_thread = new QThread(this);
    m_worker = new BaselineWorker();
    m_worker->moveToThread(m_thread);

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(m_thread, &QThread::started, m_worker, [this, index, filePaths]() {
        m_worker->Baseline_Estimate(index, filePaths);
    });
    connect(m_worker, &BaselineWorker::updateProcess, this, &Baseline::updateProcess);
    connect(m_worker, &BaselineWorker::sendBL, this, &Baseline::Paint_Baseline);
    connect(m_worker, &BaselineWorker::endProcess, this, &Baseline::endProcess);
    connect(m_worker, &BaselineWorker::errorProcess, this, [this](QString err) {
        QMessageBox::warning(this, "Error", err);
        endProcess();
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    
    // Connect stop trigger
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Baseline::StopThread);
    connect(this, &QWidget::destroyed, this, &Baseline::StopThread);

    m_thread->start();
    ChangeVision(false);
}

void Baseline::on_buttonBox_rejected()
{
    this->close();
}
