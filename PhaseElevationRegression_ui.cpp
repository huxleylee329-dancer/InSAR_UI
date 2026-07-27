#include "PhaseElevationRegression_ui.h"
#include <QThread>
#include <QMessageBox>
#include <QRegularExpression>
#include <QAbstractButton>
#include <FormatConversion.h>
#include "NodeUtils.h"
#include "icon_source.h"

PhaseElevationRegression_ui::PhaseElevationRegression_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PhaseElevationRegression),
    copy(nullptr),
    m_worker(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(true);
}

PhaseElevationRegression_ui::~PhaseElevationRegression_ui()
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

void PhaseElevationRegression_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void PhaseElevationRegression_ui::endProcess()
{
    persistGeneratedOutputs();
    if (m_worker != nullptr && m_worker->thread()->isRunning())
    {
        m_worker->thread()->quit();
        m_worker->thread()->wait();
    }
    m_worker = nullptr;
    ui->progressBar->hide();
    this->close();
}

void PhaseElevationRegression_ui::StopThread()
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

void PhaseElevationRegression_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void PhaseElevationRegression_ui::ChangeVision(bool Editable)
{
    ui->comboBox->setDisabled(!Editable);
    ui->comboBox_2->setDisabled(!Editable);
    ui->file_name->setDisabled(!Editable);
    ui->comboBox_method->setDisabled(!Editable);
    ui->lineEdit_window->setDisabled(!Editable);
    ui->doubleSpinBox_coherence->setDisabled(!Editable);
    ui->buttonBox->buttons().at(0)->setDisabled(!Editable);
}

void PhaseElevationRegression_ui::ShowProjectList(QStandardItemModel* model)
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
        if (typeTag.startsWith("phase"))
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("未检测到可处理数据，请先生成干涉相位！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);
}

void PhaseElevationRegression_ui::on_comboBox_currentIndexChanged()
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
            if (typeTag.startsWith("phase"))
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
    }
}

void PhaseElevationRegression_ui::on_buttonBox_accepted()
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

    // 校验滑动窗口输入
    bool ok = false;
    int windowSize = ui->lineEdit_window->text().toInt(&ok);
    if (!ok || windowSize < 0)
    {
        QMessageBox::warning(nullptr, "Warning!", QStringLiteral("滑动窗口尺寸应为非负整数！"));
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

    QStringList phaseNames;
    QStringList phasePaths;
    if (!copy) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("项目模型不可用。"));
        return;
    }
    const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
    if (projects.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程。"));
        return;
    }
    QStandardItem* project = projects.first();
    for (int i = 0; i < project->rowCount(); ++i) {
        QStandardItem* node = project->child(i, 0);
        if (!node || node->text() != ui->comboBox_2->currentText())
            continue;
        for (int j = 0; j < node->rowCount(); ++j) {
            QStandardItem* image = node->child(j, 0);
            QStandardItem* path = node->child(j, 1);
            if (image && path && image->toolTip() == "phase") {
                phaseNames.append(image->text());
                phasePaths.append(path->text());
            }
        }
        break;
    }
    if (phasePaths.isEmpty()) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("没有可校正的干涉图。"));
        return;
    }

    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();
    m_generatedOffsetRows.clear();
    m_generatedOffsetCols.clear();

    m_worker = new PhaseElevationRegressionWorker;
    m_worker->moveToThread(new QThread(this));

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &PhaseElevationRegression_ui::operate, m_worker, &PhaseElevationRegressionWorker::doRegression, Qt::QueuedConnection);
    connect(m_worker, &PhaseElevationRegressionWorker::updateProcess, this, &PhaseElevationRegression_ui::updateProcess);
    connect(m_worker, &PhaseElevationRegressionWorker::outputsGenerated, this,
        [this](const QStringList& names, const QStringList& paths,
               const QList<int>& rows, const QList<int>& cols) {
            m_generatedOutputNames = names;
            m_generatedOutputPaths = paths;
            m_generatedOffsetRows = rows;
            m_generatedOffsetCols = cols;
        });
    connect(m_worker, &PhaseElevationRegressionWorker::endProcess, this, &PhaseElevationRegression_ui::endProcess);
    connect(m_worker->thread(), &QThread::finished, m_worker, &PhaseElevationRegressionWorker::deleteLater);
    connect(this, &QWidget::destroyed, this, &PhaseElevationRegression_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &PhaseElevationRegression_ui::StopThread);

    m_worker->thread()->start();
    ChangeVision(false);

    int polyOrder = ui->comboBox_method->currentIndex() + 1; // 0→1(线性), 1→2(二次)
    double coherenceThresh = ui->doubleSpinBox_coherence->value();

    emit operate(polyOrder, windowSize, coherenceThresh,
                 this->save_path, ui->comboBox->currentText(),
                 ui->comboBox_2->currentText(), ui->file_name->text(),
                 phaseNames, phasePaths);
}

void PhaseElevationRegression_ui::persistGeneratedOutputs()
{
    if (!copy || m_generatedOutputNames.size() != m_generatedOutputPaths.size() ||
        m_generatedOutputPaths.size() != m_generatedOffsetRows.size() ||
        m_generatedOutputPaths.size() != m_generatedOffsetCols.size() ||
        m_generatedOutputPaths.isEmpty()) {
        return;
    }

    const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
    if (projects.isEmpty())
        return;

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), ui->file_name->text(), "phase-2.5", FOLDER_ICON);
    if (!outputNode)
        return;

    XMLFile xml;
    const QString xmlPath = save_path + "/" + ui->comboBox->currentText() + ".Insar";
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("输出已生成，但项目 XML 保存失败。"));
        return;
    }

    for (int i = 0; i < m_generatedOutputPaths.size(); ++i) {
        const QString relativePath = QString("/%1/%2.h5")
            .arg(ui->file_name->text(), m_generatedOutputNames[i]);
        NodeUtils::findOrCreateChildItem(outputNode, m_generatedOutputNames[i], "phase",
            m_generatedOutputPaths[i], IMAGEDATA_ICON);
        xml.XMLFile_add_unwrap(ui->file_name->text().toStdString().c_str(),
            m_generatedOutputNames[i].toStdString().c_str(), relativePath.toStdString().c_str(),
            m_generatedOffsetRows[i], m_generatedOffsetCols[i], "PhaseElevationRegression", 0);
    }
    xml.XMLFile_save(xmlPath.toStdString().c_str());
    emit sendCopy(copy);
}

void PhaseElevationRegression_ui::on_buttonBox_rejected()
{
    this->close();
}
