#include "MainWindow.h"
#include "import_Biomass.h"
#include "ImportTask.h"
#include "ImportOutputPersistence.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <qmessagebox.h>
#include <QThread>
#include <QFileInfo>

import_Biomass::import_Biomass(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportBiomass)
{
    ui->setupUi(this);
    import_Biomass_thread = nullptr;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    
    // Set placeholder for node name
    ui->lineEdit_dst_node->setPlaceholderText(QStringLiteral("例如: Biomass_L1A_Node"));
}

import_Biomass::~import_Biomass()
{
    import_Biomass_thread = nullptr;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

void import_Biomass::ChangeVision(bool Editable)
{
    ui->comboBox_dst_project->setEnabled(Editable);
    ui->lineEdit_dst_node->setEnabled(Editable);
    ui->buttonBox->buttons().at(0)->setEnabled(Editable); // OK button is typically at 0 or 1, disable/enable standard buttons safely
    
    ui->lineEdit_amp_file->setEnabled(Editable);
    ui->pushButton_amp_browse->setEnabled(Editable);
    
    ui->lineEdit_phase_file->setEnabled(Editable);
    ui->pushButton_phase_browse->setEnabled(Editable);
    
    ui->lineEdit_xml_file->setEnabled(Editable);
    ui->pushButton_xml_browse->setEnabled(Editable);
    
    ui->lineEdit_orbit_file->setEnabled(Editable);
    ui->pushButton_orbit_browse->setEnabled(Editable);
    
    ui->comboBox_pol->setEnabled(Editable);
    ui->pushButton_add->setEnabled(Editable);
    ui->pushButton_remove->setEnabled(Editable);
}

void import_Biomass::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void import_Biomass::endProcess()
{
    if (import_Biomass_thread)
    {
        import_Biomass_thread->thread()->quit();
        import_Biomass_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void import_Biomass::errorProcess(QString error_msg)
{
    QMessageBox::warning(nullptr, "Error", error_msg);
    if (import_Biomass_thread)
    {
        import_Biomass_thread->thread()->quit();
        import_Biomass_thread->thread()->wait();
        import_Biomass_thread = nullptr;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_Biomass::StopThread()
{
    if (import_Biomass_thread != nullptr)
    {
        if (import_Biomass_thread->thread()->isRunning())
        {
            import_Biomass_thread->thread()->requestInterruption();
            import_Biomass_thread->thread()->quit();
            import_Biomass_thread->thread()->wait();
        }
        import_Biomass_thread = nullptr;
    }
}

void import_Biomass::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_Biomass::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    ui->comboBox_dst_project->setCurrentIndex(0);
    this->save_path = model->item(0, 1)->text();
}

void import_Biomass::on_comboBox_dst_project_currentIndexChanged()
{
    if (!copy || ui->comboBox_dst_project->currentIndex() < 0) return;
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

// Browse slots
void import_Biomass::on_pushButton_amp_browse_pressed()
{
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择幅度文件"), "", "Amplitude TIFF (*.tiff *.tif)");
    if (!file.isEmpty())
        ui->lineEdit_amp_file->setText(file);
}

void import_Biomass::on_pushButton_phase_browse_pressed()
{
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择相位文件"), "", "Phase TIFF (*.tiff *.tif)");
    if (!file.isEmpty())
        ui->lineEdit_phase_file->setText(file);
}

void import_Biomass::on_pushButton_xml_browse_pressed()
{
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择参数 XML 文件"), "", "XML (*.xml)");
    if (!file.isEmpty())
        ui->lineEdit_xml_file->setText(file);
}

void import_Biomass::on_pushButton_orbit_browse_pressed()
{
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择轨道文件"), "", "Orbit TIFF (*.tiff *.tif)");
    if (!file.isEmpty())
        ui->lineEdit_orbit_file->setText(file);
}

// List management
void import_Biomass::on_pushButton_add_pressed()
{
    QString amp = ui->lineEdit_amp_file->text().trimmed();
    QString phase = ui->lineEdit_phase_file->text().trimmed();
    QString xml = ui->lineEdit_xml_file->text().trimmed();
    QString orbit = ui->lineEdit_orbit_file->text().trimmed();
    QString pol = ui->comboBox_pol->currentText();

    if (amp.isEmpty() || phase.isEmpty() || xml.isEmpty() || orbit.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请将所有文件字段填写完整后再进行添加！"));
        return;
    }

    if (!QFile::exists(amp) || !QFile::exists(phase) || !QFile::exists(xml) || !QFile::exists(orbit))
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("所选文件路径中存在不存在的文件，请重新检查！"));
        return;
    }

    QFileInfo xmlInfo(xml);
    QString baseName = xmlInfo.baseName();
    QString importName = baseName + "_" + pol;

    // Show in list widget
    QString displayStr = QString("%1 (%2)").arg(importName).arg(pol);
    ui->listWidget->addItem(displayStr);

    // Add to vectors
    m_ampFiles.push_back(amp);
    m_phaseFiles.push_back(phase);
    m_xmlFiles.push_back(xml);
    m_orbitFiles.push_back(orbit);
    m_polarizations.push_back(pol);
    m_importNamelist.push_back(importName);

    // Clear file edits for next entry
    ui->lineEdit_amp_file->clear();
    ui->lineEdit_phase_file->clear();
    ui->lineEdit_xml_file->clear();
    ui->lineEdit_orbit_file->clear();
}

void import_Biomass::on_pushButton_remove_pressed()
{
    int currRow = ui->listWidget->currentRow();
    if (currRow >= 0 && currRow < (int)m_ampFiles.size())
    {
        ui->listWidget->takeItem(currRow);
        m_ampFiles.erase(m_ampFiles.begin() + currRow);
        m_phaseFiles.erase(m_phaseFiles.begin() + currRow);
        m_xmlFiles.erase(m_xmlFiles.begin() + currRow);
        m_orbitFiles.erase(m_orbitFiles.begin() + currRow);
        m_polarizations.erase(m_polarizations.begin() + currRow);
        m_importNamelist.erase(m_importNamelist.begin() + currRow);
    }
}

void import_Biomass::on_buttonBox_rejected()
{
    close();
}

void import_Biomass::on_buttonBox_accepted()
{
    if (m_ampFiles.empty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("导入数据列表不能为空！请先添加配置好的数据。"));
        return;
    }

    QString dstNode = ui->lineEdit_dst_node->text().trimmed();
    if (dstNode.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("目标节点名不能为空！"));
        return;
    }

    // Check duplicate nodes in project
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (dstNode == project->child(i, 0)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
            break;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    // Setup and start thread
    StopThread();

    import_Biomass_thread = new BiomassImportWorker;
    QThread* thread = new QThread(this);
    import_Biomass_thread->moveToThread(thread);

    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(import_Biomass_thread, &BiomassImportWorker::updateProcess, this, &import_Biomass::updateProcess);
    connect(thread, &QThread::finished, import_Biomass_thread, &BiomassImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_Biomass_thread, &BiomassImportWorker::endProcess, this, &import_Biomass::endProcess);
    connect(import_Biomass_thread, &BiomassImportWorker::errorProcess, this, &import_Biomass::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_Biomass::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_Biomass::StopThread);
    connect(import_Biomass_thread, &BiomassImportWorker::outputsGenerated, this,
        [this, projectName = ui->comboBox_dst_project->currentText(), savePath = save_path](const QString& dstNode, const QStringList& names, const QStringList& paths, const QString& dataType, const QString& format) {
            if (ImportOutputPersistence::persist(copy, projectName, savePath, dstNode, names, paths, dataType, format)) TransitModel(copy);
        });

    thread->start();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < m_ampFiles.size(); ++i) {
        ImportTask task;
        task.filename = m_importNamelist[i];
        task.arguments = QStringList{ m_ampFiles[i], m_phaseFiles[i], m_xmlFiles[i], m_orbitFiles[i], m_polarizations[i] };
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(import_Biomass_thread, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, dstNode));

    ChangeVision(false);
}
