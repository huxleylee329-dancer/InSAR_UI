#include "IonosphericCorrection_ui.h"
#include <QThread>
#include <QMessageBox>
#include <QRegularExpression>
#include <QAbstractButton>
#include "icon_source.h"

IonosphericCorrection_ui::IonosphericCorrection_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::IonosphericCorrection),
    copy(nullptr),
    m_worker(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(true);
}

IonosphericCorrection_ui::~IonosphericCorrection_ui()
{
    emit sendCopy(copy);
    m_worker = nullptr;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

void IonosphericCorrection_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void IonosphericCorrection_ui::endProcess()
{
    if (m_worker != nullptr && m_worker->thread()->isRunning())
    {
        m_worker->thread()->quit();
        m_worker->thread()->wait();
    }
    m_worker = nullptr;
    ui->progressBar->hide();
    this->close();
}

void IonosphericCorrection_ui::StopThread()
{
    if (m_worker != nullptr)
    {
        if (m_worker->thread()->isRunning())
        {
            m_worker->thread()->requestInterruption();
            m_worker->thread()->quit();
            m_worker->thread()->wait();
        }
        m_worker = nullptr;
    }
}

void IonosphericCorrection_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void IonosphericCorrection_ui::ChangeVision(bool Editable)
{
    ui->comboBox->setDisabled(!Editable);
    ui->comboBox_2->setDisabled(!Editable);
    ui->file_name->setDisabled(!Editable);
    ui->doubleSpinBox_subband->setDisabled(!Editable);
    ui->doubleSpinBox_filter->setDisabled(!Editable);
    ui->checkBox_tec->setDisabled(!Editable);
    ui->buttonBox->buttons().at(0)->setDisabled(!Editable);
}

void IonosphericCorrection_ui::ShowProjectList(QStandardItemModel* model)
{
    if (!model) return;
    if (model->rowCount() < 1) return;
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = nullptr;
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
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    for (int i = 0; i < count; i++)
    {
        QString typeTag = model->data(model->index(i, 1, pro_index)).toString();
        // 电离层校正需要已配准的 SLC 数据（complex-2.0 或 complex-3.0）
        if (typeTag == "complex-2.0" || typeTag == "complex-3.0")
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("未检测到可处理数据，请先进行 SLC 配准！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);
}

void IonosphericCorrection_ui::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_2->clear();
        for (int i = 0; i < count; i++)
        {
            QString typeTag = copy->data(copy->index(i, 1, pro_index)).toString();
            if (typeTag == "complex-2.0" || typeTag == "complex-3.0")
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
    }
}

void IonosphericCorrection_ui::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("当前无可用数据节点！"));
        return;
    }

    if (m_worker != nullptr)
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("任务正在执行中，请勿重复操作！"));
        return;
    }

    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("请输入目标文件夹名称！"));
        return;
    }
    if (!ui->file_name->text().contains(QRegularExpression("^\\w+$")))
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }

    m_worker = new IonosphericCorrectionWorker;
    m_worker->moveToThread(new QThread(this));

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &IonosphericCorrection_ui::operate, m_worker, &IonosphericCorrectionWorker::doCorrection, Qt::QueuedConnection);
    connect(m_worker, &IonosphericCorrectionWorker::updateProcess, this, &IonosphericCorrection_ui::updateProcess);
    connect(m_worker, &IonosphericCorrectionWorker::endProcess, this, &IonosphericCorrection_ui::endProcess);
    connect(m_worker, &IonosphericCorrectionWorker::sendModel, this, &IonosphericCorrection_ui::TransitModel);
    connect(m_worker->thread(), &QThread::finished, m_worker, &IonosphericCorrectionWorker::deleteLater);
    connect(this, &QWidget::destroyed, this, &IonosphericCorrection_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &IonosphericCorrection_ui::StopThread);

    m_worker->thread()->start();
    ChangeVision(false);

    emit operate(ui->doubleSpinBox_subband->value(),
                 ui->doubleSpinBox_filter->value(),
                 ui->checkBox_tec->isChecked(),
                 this->save_path, ui->comboBox->currentText(),
                 ui->comboBox_2->currentText(), ui->file_name->text(),
                 this->copy);
}

void IonosphericCorrection_ui::on_buttonBox_rejected()
{
    this->close();
}
