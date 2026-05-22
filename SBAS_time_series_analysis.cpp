#include"SBAS_time_series_analysis.h"
#include"ui_SbasTimeSeriesAnalysis.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<QFileDialog>
#include<Utils.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
SBAS_time_series_analysis::SBAS_time_series_analysis(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::SbasTimeSeriesAnalysis)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();
    ui->radioButton_Delaunay_MCF->setChecked(true);
    ui->doubleSpinBox_temporal_thresh->setMinimum(0.0);
    ui->doubleSpinBox_temporal_thresh->setMaximum(2000.0);
    ui->doubleSpinBox_temporal_thresh->setValue(200.0);
    ui->doubleSpinBox_temporal_thresh_low->setMinimum(0.0);
    ui->doubleSpinBox_temporal_thresh_low->setMaximum(1000.0);
    ui->doubleSpinBox_temporal_thresh_low->setValue(0.0);
    ui->doubleSpinBox_spatial_thresh->setMinimum(0.0);
    ui->doubleSpinBox_spatial_thresh->setMaximum(10000.0);
    ui->doubleSpinBox_spatial_thresh->setValue(500.0);
    ui->doubleSpinBox_reflattening_coh_thresh->setMinimum(0.0);
    ui->doubleSpinBox_reflattening_coh_thresh->setMaximum(1.0);
    ui->doubleSpinBox_reflattening_coh_thresh->setValue(0.8);
    ui->doubleSpinBox_reflattening_def_thresh->setMinimum(0.0);
    ui->doubleSpinBox_reflattening_def_thresh->setValue(0.01);
    ui->spinBox_multilook_az->setMinimum(2);
    ui->spinBox_multilook_az->setMaximum(50);
    ui->spinBox_multilook_rg->setMinimum(2);
    ui->spinBox_multilook_rg->setMaximum(50);
    ui->spinBox_multilook_az->setValue(4);
    ui->spinBox_multilook_rg->setValue(4);
    ui->doubleSpinBox_coherence_thresh->setMinimum(0.0);
    ui->doubleSpinBox_coherence_thresh->setMaximum(1.0);
    ui->doubleSpinBox_coherence_thresh->setValue(0.5);
    ui->doubleSpinBox_temporal_coherence_thresh->setMinimum(0.0);
    ui->doubleSpinBox_temporal_coherence_thresh->setMaximum(1.0);
    ui->doubleSpinBox_temporal_coherence_thresh->setValue(0.6);
    ui->doubleSpinBox_Goldstein_alpha->setMinimum(0.0);
    ui->doubleSpinBox_Goldstein_alpha->setMaximum(1.0);
    ui->doubleSpinBox_Goldstein_alpha->setValue(0.8);
}
SBAS_time_series_analysis::~SBAS_time_series_analysis()
{
    if (copy)
    {
        if (copy->findItems(ui->comboBox_project->currentText())[0])
            copy->findItems(ui->comboBox_project->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    SBAS_time_series_analysis_thread = NULL;
}

void SBAS_time_series_analysis::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void SBAS_time_series_analysis::endProcess()
{
    SBAS_time_series_analysis_thread->thread()->quit();
    SBAS_time_series_analysis_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void SBAS_time_series_analysis::endThread()
{
    SBAS_time_series_analysis_thread->thread()->quit();
    SBAS_time_series_analysis_thread->thread()->wait();
}
void SBAS_time_series_analysis::StopThread()
{
    if (SBAS_time_series_analysis_thread != NULL)
        if (SBAS_time_series_analysis_thread->thread()->isRunning())
        {
            SBAS_time_series_analysis_thread->thread()->requestInterruption();
            SBAS_time_series_analysis_thread->thread()->quit();
            SBAS_time_series_analysis_thread->thread()->wait();
        }

}

void SBAS_time_series_analysis::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void SBAS_time_series_analysis::ShowProjectList(QStandardItemModel* model)
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
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        ui->comboBox_srcNode->clear();
        return;
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    ui->comboBox_srcNode->clear();
    for (int i = 0; i < count; i++)
    {
        if (project->child(i, 1)->text() == QString("complex-3.0"))
        {
            ui->comboBox_srcNode->addItem(project->child(i, 0)->text());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }

        }
    }
    if (!node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
        ui->comboBox_srcNode->clear();
        return;
    }
    ui->comboBox_srcNode->setCurrentIndex(0);
}
void SBAS_time_series_analysis::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        bool isnodefound = false;
        QStandardItem* node = NULL;
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        ui->comboBox_srcNode->clear();
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 1)->text() == QString("complex-3.0"))
            {
                ui->comboBox_srcNode->addItem(project->child(i, 0)->text());
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
            ui->comboBox_srcNode->clear();
            return;
        }
    }
}



void SBAS_time_series_analysis::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project->setDisabled(0);
        ui->comboBox_srcNode->setDisabled(0);
        ui->lineEdit_dstNode->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->doubleSpinBox_temporal_thresh->setDisabled(0);
        ui->doubleSpinBox_temporal_thresh_low->setDisabled(0);
        ui->doubleSpinBox_spatial_thresh->setDisabled(0);
        ui->doubleSpinBox_coherence_thresh->setDisabled(0);
        ui->doubleSpinBox_temporal_coherence_thresh->setDisabled(0);
        ui->doubleSpinBox_Goldstein_alpha->setDisabled(0);
        ui->doubleSpinBox_reflattening_coh_thresh->setDisabled(0);
        ui->doubleSpinBox_reflattening_def_thresh->setDisabled(0);
        ui->radioButton_Delaunay_MCF->setDisabled(0);
        ui->radioButton_MCF->setDisabled(0);
        ui->radioButton_SNAPHU->setDisabled(0);
        ui->spinBox_multilook_az->setDisabled(0);
        ui->spinBox_multilook_rg->setDisabled(0);
    }
    else
    {
        ui->comboBox_project->setDisabled(1);
        ui->comboBox_srcNode->setDisabled(1);
        ui->lineEdit_dstNode->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->doubleSpinBox_temporal_thresh->setDisabled(1);
        ui->doubleSpinBox_temporal_thresh_low->setDisabled(1);
        ui->doubleSpinBox_spatial_thresh->setDisabled(1);
        ui->doubleSpinBox_coherence_thresh->setDisabled(1);
        ui->doubleSpinBox_Goldstein_alpha->setDisabled(1);
        ui->doubleSpinBox_temporal_coherence_thresh->setDisabled(1);
        ui->doubleSpinBox_reflattening_coh_thresh->setDisabled(1);
        ui->doubleSpinBox_reflattening_def_thresh->setDisabled(1);
        ui->radioButton_Delaunay_MCF->setDisabled(1);
        ui->radioButton_MCF->setDisabled(1);
        ui->radioButton_SNAPHU->setDisabled(1);
        ui->spinBox_multilook_az->setDisabled(1);
        ui->spinBox_multilook_rg->setDisabled(1);
    }


}


void SBAS_time_series_analysis::on_comboBox_srcNode_currentIndexChanged()
{
    
    if (ui->comboBox_srcNode->count() > 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QString temp = project->child(i, 0)->text();
            if (project->child(i, 0)->text() == ui->comboBox_srcNode->currentText())
            {
                node = project->child(i, 0); break;
            }
        }

        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            return;
        }
    }
}

void SBAS_time_series_analysis::on_buttonbrowse_triggered()
{
    QString save_path = QFileDialog::getSaveFileName(this,
        QStringLiteral("csv另存为"),
        "/",
        "*.csv");
    ui->csv_path->setText(save_path);
}

void SBAS_time_series_analysis::on_buttonBox_accepted()
{
    bool bFlag = false;
    if (copy->item(ui->comboBox_project->currentIndex(), 0)->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    //防重名检查
    if (ui->lineEdit_dstNode->text().isEmpty()) return;
    QStandardItem* project = this->copy->findItems(ui->comboBox_project->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstNode->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }
    if (ui->csv_path->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入新建工程名称（该名称应为数字、字母及下划线的组合）！"));
        return;
    }
    bFlag = ui->csv_path->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意工程名称应当为数字、字母及下划线的组合！"));
        return;
    }
    if (ui->radioButton_MCF->isChecked())
    {
        this->method = 3;
    }
    else if (ui->radioButton_SNAPHU->isChecked())
    {
        this->method = 2;
    }
    else
    {
        this->method = 1;
    }
    SBAS_time_series_analysis_thread = new MyThread;
    SBAS_time_series_analysis_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &SBAS_time_series_analysis::operate, SBAS_time_series_analysis_thread, &MyThread::SBAS_time_series, Qt::QueuedConnection);
    connect(SBAS_time_series_analysis_thread, &MyThread::updateProcess, this, &SBAS_time_series_analysis::updateProcess);
    connect(SBAS_time_series_analysis_thread->thread(), &QThread::finished, SBAS_time_series_analysis_thread, &MyThread::deleteLater);
    connect(SBAS_time_series_analysis_thread, &MyThread::endProcess, this, &SBAS_time_series_analysis::endProcess);
    connect(this, &QWidget::destroyed, this, &SBAS_time_series_analysis::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SBAS_time_series_analysis::StopThread);// , Qt::QueuedConnection);
    connect(SBAS_time_series_analysis_thread, &MyThread::sendModel, this, &SBAS_time_series_analysis::TransitModel);
    SBAS_time_series_analysis_thread->thread()->start();
    ChangeVision(false);
    emit operate(
        ui->doubleSpinBox_temporal_thresh_low->value(),
        ui->doubleSpinBox_temporal_thresh->value(),
        ui->doubleSpinBox_spatial_thresh->value(),
        ui->spinBox_multilook_rg->value(),
        ui->spinBox_multilook_az->value(),
        this->method,
        ui->doubleSpinBox_Goldstein_alpha->value(),
        ui->doubleSpinBox_coherence_thresh->value(),
        ui->doubleSpinBox_temporal_coherence_thresh->value(),
        ui->doubleSpinBox_reflattening_coh_thresh->value(),
        ui->doubleSpinBox_reflattening_def_thresh->value(),
        ui->comboBox_project->currentText(),
        ui->comboBox_srcNode->currentText(),
        ui->lineEdit_dstNode->text(),
        ui->csv_path->text(),
        this->copy
    );

}

void SBAS_time_series_analysis::on_buttonBox_rejected()
{
    this->close();
}
