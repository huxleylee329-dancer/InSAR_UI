#include "DeformationRateField_ui.h"
#include "ui_DeformationRateField.h"
#include "icon_source.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include <QMessageBox>
#include <QThread>
#include <QCoreApplication>
#include <QApplication>
#include <QFileInfo>

DeformationRateField_ui::DeformationRateField_ui(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::DeformationRateField)
    , copy(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();

    ui->comboBox_modelType->addItem(QStringLiteral("线性拟合"), 1);
    ui->comboBox_modelType->addItem(QStringLiteral("二次多项式"), 2);

    ui->comboBox_confidenceLevel->addItem("90%", 0.90);
    ui->comboBox_confidenceLevel->addItem("95%", 0.95);
    ui->comboBox_confidenceLevel->addItem("99%", 0.99);
    ui->comboBox_confidenceLevel->setCurrentIndex(1); // 95%

    ui->comboBox_colorMap->addItem(QStringLiteral("蓝-白-红"), 0);
    ui->comboBox_colorMap->addItem(QStringLiteral("热力图"), 1);
    ui->comboBox_colorMap->addItem(QStringLiteral("彩虹色"), 2);

    connect(ui->comboBox_project, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DeformationRateField_ui::on_comboBox_project_currentIndexChanged);
}

DeformationRateField_ui::~DeformationRateField_ui()
{
    if (copy && ui->comboBox_project->count() > 0) {
        auto items = copy->findItems(ui->comboBox_project->currentText());
        if (!items.isEmpty() && items[0]) {
            items[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    StopThread();
    delete ui;
}

void DeformationRateField_ui::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    ui->comboBox_project->clear();
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_project->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }

    if (model->rowCount() == 0) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        ui->comboBox_srcNode->clear();
        return;
    }

    updateSrcNodeCombo();
}

void DeformationRateField_ui::on_comboBox_project_currentIndexChanged()
{
    updateSrcNodeCombo();
}

void DeformationRateField_ui::updateSrcNodeCombo()
{
    ui->comboBox_srcNode->clear();
    if (!copy || ui->comboBox_project->count() == 0) return;

    auto items = copy->findItems(ui->comboBox_project->currentText());
    if (items.isEmpty() || !items[0]) return;
    QStandardItem* project = items[0];
    this->save_path = copy->item(project->row(), 1)->text();

    for (int i = 0; i < project->rowCount(); i++) {
        QStandardItem* child_col0 = project->child(i, 0);
        QStandardItem* child_col1 = project->child(i, 1);
        if (child_col1 && child_col1->text() == "SBAS-1.0") {
            for (int j = 0; j < child_col0->rowCount(); ++j) {
                QStandardItem* leaf_col0 = child_col0->child(j, 0);
                if (leaf_col0) {
                    ui->comboBox_srcNode->addItem(QString("%1/%2").arg(child_col0->text()).arg(leaf_col0->text()));
                }
            }
        }
    }
}

QStringList DeformationRateField_ui::getSelectedFilePaths()
{
    QStringList paths;
    if (!copy || ui->comboBox_srcNode->count() == 0) return paths;

    QString current = ui->comboBox_srcNode->currentText();
    QStringList parts = current.split('/');
    if (parts.size() < 2) return paths;

    auto items = copy->findItems(ui->comboBox_project->currentText());
    if (items.isEmpty() || !items[0]) return paths;
    QStandardItem* project = items[0];

    for (int i = 0; i < project->rowCount(); i++) {
        QStandardItem* child_col0 = project->child(i, 0);
        if (child_col0 && child_col0->text() == parts[0]) {
            for (int j = 0; j < child_col0->rowCount(); ++j) {
                QStandardItem* leaf_col0 = child_col0->child(j, 0);
                QStandardItem* leaf_col1 = child_col0->child(j, 1);
                if (leaf_col0 && leaf_col0->text() == parts[1] && leaf_col1) {
                    paths.append(leaf_col1->text());
                    return paths;
                }
            }
        }
    }
    return paths;
}

void DeformationRateField_ui::updateProcess(int progress, QString message)
{
    ui->progressBar->setValue(progress);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(message).arg(progress));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void DeformationRateField_ui::endProcess()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void DeformationRateField_ui::StopThread()
{
    if (m_worker && m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void DeformationRateField_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void DeformationRateField_ui::setControlsEnabled(bool enabled)
{
    ui->comboBox_project->setEnabled(enabled);
    ui->comboBox_srcNode->setEnabled(enabled);
    ui->comboBox_modelType->setEnabled(enabled);
    ui->comboBox_confidenceLevel->setEnabled(enabled);
    ui->lineEdit_cohThreshHigh->setEnabled(enabled);
    ui->lineEdit_cohThreshMid->setEnabled(enabled);
    ui->lineEdit_uncertaintyThreshHigh->setEnabled(enabled);
    ui->lineEdit_uncertaintyThreshMid->setEnabled(enabled);
    ui->comboBox_colorMap->setEnabled(enabled);
    ui->checkBox_showContour->setEnabled(enabled);
    ui->lineEdit_contourInterval->setEnabled(enabled);
    ui->checkBox_showArrow->setEnabled(enabled);
    ui->lineEdit_arrowSpacing->setEnabled(enabled);
    ui->lineEdit_dstNode->setEnabled(enabled);
    ui->buttonBox->buttons().at(0)->setEnabled(enabled);
}

void DeformationRateField_ui::on_buttonBox_accepted()
{
    if (!copy || ui->comboBox_project->count() == 0) return;

    auto items = copy->findItems(ui->comboBox_project->currentText());
    if (items.isEmpty() || !items[0]) return;
    QStandardItem* project_item = items[0];

    if (project_item->rowCount() == 0) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("该工程下未检测到数据！请先进行 SBAS 时序分析或更换工程！"));
        return;
    }

    if (ui->lineEdit_dstNode->text().isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("目标节点名不能为空！"));
        return;
    }

    for (int i = 0; i < project_item->rowCount(); i++) {
        if (ui->lineEdit_dstNode->text() == project_item->child(i)->text()) {
            QMessageBox::warning(this, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }

    QStringList filePaths = getSelectedFilePaths();
    if (filePaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("无法获取时序数据 H5 文件路径！"));
        return;
    }

    m_worker = new DeformationRateFieldWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &DeformationRateField_ui::operate, m_worker, &DeformationRateFieldWorker::analyze_rate_field, Qt::QueuedConnection);
    connect(m_worker, &DeformationRateFieldWorker::updateProcess, this, &DeformationRateField_ui::updateProcess);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    connect(m_worker, &DeformationRateFieldWorker::endProcess, this, &DeformationRateField_ui::endProcess);
    connect(m_worker, &DeformationRateFieldWorker::errorProcess, this, [this](QString err) {
        QMessageBox::warning(this, "Error", err);
        StopThread();
    });
    connect(this, &QWidget::destroyed, this, &DeformationRateField_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &DeformationRateField_ui::StopThread);
    connect(m_worker, &DeformationRateFieldWorker::sendModel, this, &DeformationRateField_ui::TransitModel);

    m_thread->start();
    setControlsEnabled(false);

    QString projPath = copy->item(project_item->row(), 1)->text();

    emit operate(
        projPath,
        ui->comboBox_project->currentText(),
        ui->lineEdit_dstNode->text(),
        filePaths,
        ui->comboBox_modelType->currentData().toInt(),
        ui->comboBox_confidenceLevel->currentData().toDouble(),
        ui->lineEdit_cohThreshHigh->text().toDouble(),
        ui->lineEdit_cohThreshMid->text().toDouble(),
        ui->lineEdit_uncertaintyThreshHigh->text().toDouble(),
        ui->lineEdit_uncertaintyThreshMid->text().toDouble(),
        ui->comboBox_colorMap->currentData().toInt(),
        ui->checkBox_showContour->isChecked(),
        ui->lineEdit_contourInterval->text().toInt(),
        ui->checkBox_showArrow->isChecked(),
        ui->lineEdit_arrowSpacing->text().toInt(),
        this->copy
    );
}

void DeformationRateField_ui::on_buttonBox_rejected()
{
    this->close();
}
