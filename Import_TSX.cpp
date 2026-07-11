#include"MainWindow.h"
#include"Import_TSX.h"
#include"ImportTask.h"
#include"icon_source.h"
#include"qfiledialog.h"
#include "NodeUtils.h"
#include<opencv2/highgui.hpp>
#include<qmessagebox.h>
Import_TSX::Import_TSX(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportTsx)
{
    ui->setupUi(this);
    import_TSX_thread = NULL;
    import_TSX_thread2 = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);
    ui->comboBox_pol->addItem(QString("HH"));
    ui->comboBox_pol->addItem(QString("VV"));
    ui->comboBox_pol2->addItem(QString("HH"));
    ui->comboBox_pol2->addItem(QString("VV"));
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.beginGroup("TSX");
    this->xml_path = settings.value("xml_Path").toString();
    settings.endGroup();
}
Import_TSX::~Import_TSX()
{
    import_TSX_thread = NULL;
    import_TSX_thread2 = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->comboBox_dst_project_2->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project_2->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project_2->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

bool Import_TSX::generate_name(QListWidget* imageslist, std::vector<QString>& original_nameslist, std::vector<QString>& import_nameslist)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        original_nameslist.push_back(imageslist->item(i)->text());
        QFileInfo fileinfo = QFileInfo(imageslist->item(i)->text());
        string tmp = fileinfo.baseName().toStdString();
        if (tmp.rfind("T") < tmp.length() && tmp.rfind("T") >= 0)
        {
            int start = tmp.rfind("T") - 8;
            start = start < 0 ? 0 : start;
            import_nameslist.push_back(QString(tmp.substr(start, 8).c_str()));
        }
        else
        {
            import_nameslist.push_back(fileinfo.baseName());
        }
    }
    return true;
}

void Import_TSX::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->comboBox_dst_project_2->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->lineEdit_dst_node_2->setDisabled(0);
        ui->LineEdit_dst_filename->setDisabled(0);
        ui->LineEdit_xml->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
        ui->button_xml_browse->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
        ui->comboBox_pol->setDisabled(0);
        ui->comboBox_pol2->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->comboBox_dst_project_2->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->lineEdit_dst_node_2->setDisabled(1);
        ui->LineEdit_dst_filename->setDisabled(1);
        ui->LineEdit_xml->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
        ui->button_xml_browse->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
        ui->comboBox_pol->setDisabled(1);
        ui->comboBox_pol2->setDisabled(1);
    }
}

void Import_TSX::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    else if (!ui->progressBar_2->isHidden())
    {
        ui->progressBar_2->setValue(value);
        ui->progressBar_2->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar_2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void Import_TSX::endProcess()
{
    if (import_TSX_thread)
    {
        import_TSX_thread->thread()->quit();
        import_TSX_thread->thread()->wait();

    }
    if (import_TSX_thread2)
    {
        import_TSX_thread2->thread()->quit();
        import_TSX_thread2->thread()->wait();

    }
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    this->close();
}
void Import_TSX::endThread()
{
    if (import_TSX_thread)
    {
        import_TSX_thread->thread()->quit();
        import_TSX_thread->thread()->wait();
    }
    if (import_TSX_thread2)
    {
        import_TSX_thread2->thread()->quit();
        import_TSX_thread2->thread()->wait();

    }
}
void Import_TSX::StopThread()
{
    if (import_TSX_thread != NULL)
        if (import_TSX_thread->thread()->isRunning())
        {
            import_TSX_thread->thread()->requestInterruption();
            import_TSX_thread->thread()->quit();
            import_TSX_thread->thread()->wait();
        }
    if (import_TSX_thread2 != NULL)
        if (import_TSX_thread2->thread()->isRunning())
        {
            import_TSX_thread2->thread()->requestInterruption();
            import_TSX_thread2->thread()->quit();
            import_TSX_thread2->thread()->wait();
        }
}
void Import_TSX::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void Import_TSX::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}
void Import_TSX::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i,0)->text());
        ui->comboBox_dst_project_2->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    ui->comboBox_dst_project->setCurrentIndex(0);
    ui->comboBox_dst_project_2->setCurrentIndex(0);
    this->save_path=model->item(0,1)->text();
}

void Import_TSX::on_comboBox_dst_project_2_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project_2->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void Import_TSX::on_button_xml_browse_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("导入 TerraSAR-X/TanDEM-X 数据"),
        this->xml_path,
        "*.xml");
    if (QFile::exists(filename))
    {
        ui->LineEdit_xml->setText(filename);
        QFileInfo fileinfo = QFileInfo(filename);
        string tmp = fileinfo.baseName().toStdString();
        if (tmp.rfind("T") < tmp.length() && tmp.rfind("T") >= 0)
        {
            int start = tmp.rfind("T") - 8;
            start = start < 0 ? 0 : start;
            ui->LineEdit_dst_filename->setText(QString(tmp.substr(start, 8).c_str()));
        }
        else
        {
            ui->LineEdit_dst_filename->setText(fileinfo.baseName());
        }
        
    }
}

void Import_TSX::saveSystemSettings()
{
    int count = 0;
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.beginGroup("TSX");
     settings.setValue("xml_Path", ui->LineEdit_xml->text());
    settings.endGroup();
}
void Import_TSX::on_pushButton_add_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("导入 TerraSAR-X/TanDEM-X 数据"),
        "",
        "*.xml");
    ui->listWidget->addItem(filename);
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
        {
            ui->listWidget->takeItem(i);
        }
    }
}
void Import_TSX::on_pushButton_remove_pressed()
{
    if (ui->listWidget->count() >= 1)
    {
        ui->listWidget->takeItem(ui->listWidget->currentRow());
    }
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
        {
            ui->listWidget->takeItem(i);
        }
    }
}
void Import_TSX::on_buttonBox_accepted()
{
    if (ui->LineEdit_xml->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入TerrSAR xml文件！"));
        return;
    }
    bool bFlag = ui->LineEdit_xml->text().contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意路径中应当仅包含数字、字母及下划线！"));
        return;
    }
    if (ui->LineEdit_dst_filename->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入想要保存的图片名称！"));
        return;
    }
    bFlag = ui->LineEdit_dst_filename->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意图像名称应当为数字、字母及下划线的组合！"));
        return;
    }

    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点名为空！"));
        return;
    }

    //防重名检查

    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dst_node->text() == project->child(i)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    // Ensure previous threads are released
    if (import_TSX_thread) {
        import_TSX_thread->thread()->quit();
        import_TSX_thread->thread()->wait();
    }

    import_TSX_thread = new TSXImportWorker;
    QThread* thread = new QThread(this);
    import_TSX_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(import_TSX_thread, &TSXImportWorker::updateProcess, this, &Import_TSX::updateProcess);
    connect(thread, &QThread::finished, import_TSX_thread, &TSXImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_TSX_thread, &TSXImportWorker::endProcess, this, &Import_TSX::endProcess);
    connect(this, &QWidget::destroyed, this, &Import_TSX::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Import_TSX::StopThread);// , Qt::QueuedConnection);
    connect(import_TSX_thread, &TSXImportWorker::sendModel, this, &Import_TSX::TransitModel);
    thread->start();

    // 构造 ImportTask
    std::vector<ImportTask> tasks;
    ImportTask task;
    task.filename = ui->LineEdit_dst_filename->text();
    task.arguments = QStringList{ ui->LineEdit_xml->text(), ui->comboBox_pol->currentText() };
    tasks.push_back(task);

    QMetaObject::invokeMethod(import_TSX_thread, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node->text()),
        Q_ARG(QString, ui->comboBox_dst_project->currentText()),
        Q_ARG(QStandardItemModel*, this->copy));
    ChangeVision(false);

}
void Import_TSX::on_buttonBox_rejected()
{
    close();
}

void Import_TSX::on_buttonBox_2_accepted()
{
    //检查导入文件list是否为空
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("导入图像文件为空！"));
        return;
    }
    //检查目标节点名
    if (ui->lineEdit_dst_node_2->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点名为空！"));
        return;
    }
    

    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project_2->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dst_node_2->text() == project->child(i, 0)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    //根据原始文件日期生成导入文件名称
    std::vector<QString> original_namelist;
    std::vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist)) return;


    // Ensure previous threads are released
    if (import_TSX_thread2) {
        import_TSX_thread2->thread()->quit();
        import_TSX_thread2->thread()->wait();
    }

    import_TSX_thread2 = new TSXImportWorker;
    QThread* thread2 = new QThread(this);
    import_TSX_thread2->moveToThread(thread2);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(import_TSX_thread2, &TSXImportWorker::updateProcess, this, &Import_TSX::updateProcess);
    connect(thread2, &QThread::finished, import_TSX_thread2, &TSXImportWorker::deleteLater);
    connect(thread2, &QThread::finished, thread2, &QThread::deleteLater);
    connect(import_TSX_thread2, &TSXImportWorker::endProcess, this, &Import_TSX::endProcess);
    connect(this, &QWidget::destroyed, this, &Import_TSX::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &Import_TSX::StopThread);// , Qt::QueuedConnection);
    connect(import_TSX_thread2, &TSXImportWorker::sendModel, this, &Import_TSX::TransitModel);
    thread2->start();

    // 构造 ImportTask 列表
    QString polarization = ui->comboBox_pol2->currentText();
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < original_namelist.size(); ++i) {
        ImportTask task;
        task.filename = import_namelist[i];
        task.arguments = QStringList{ original_namelist[i], polarization };
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(import_TSX_thread2, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node_2->text()),
        Q_ARG(QString, ui->comboBox_dst_project_2->currentText()),
        Q_ARG(QStandardItemModel*, this->copy));
    ChangeVision(false);
}

void Import_TSX::on_buttonBox_2_rejected()
{
    close();
}
