#include"MainWindow.h"
#include"Import_TSX.h"
#include"icon_source.h"
#include"qfiledialog.h"
#include<opencv2/highgui.hpp>
#include<qmessagebox.h>
//#include<FormatConversion.h>
//#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
//#pragma comment(lib, "FormatConversion_d.lib")
//#endif
Import_TSX::Import_TSX(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Import_TSX)
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
    QSettings settings(QString("Config.ini"), QSettings::IniFormat);
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

bool Import_TSX::generate_name(QListWidget* imageslist, vector<QString>& original_nameslist, vector<QString>& import_nameslist)
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
        QString::fromLocal8Bit("导入 TerraSAR-X/TanDEM-X 数据"),
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
    QSettings settings(QString("Config.ini"), QSettings::IniFormat);
    settings.beginGroup("TSX");
     settings.setValue("xml_Path", ui->LineEdit_xml->text());
    settings.endGroup();
}
void Import_TSX::on_pushButton_add_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QString::fromLocal8Bit("导入 TerraSAR-X/TanDEM-X 数据"),
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入TerrSAR xml文件！"));
        return;
    }
    bool bFlag = ui->LineEdit_xml->text().contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意路径中应当仅包含数字、字母及下划线！"));
        return;
    }
    if (ui->LineEdit_dst_filename->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入想要保存的图片名称！"));
        return;
    }
    bFlag = ui->LineEdit_dst_filename->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意图像名称应当为数字、字母及下划线的组合！"));
        return;
    }

    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点名为空！"));
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    import_TSX_thread = new MyThread;
    import_TSX_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &Import_TSX::operate, import_TSX_thread, &MyThread::import_TSX, Qt::QueuedConnection);
    connect(import_TSX_thread, &MyThread::updateProcess, this, &Import_TSX::updateProcess);
    connect(import_TSX_thread->thread(), &QThread::finished, import_TSX_thread, &MyThread::deleteLater);
    connect(import_TSX_thread, &MyThread::endProcess, this, &Import_TSX::endProcess);
    connect(this, &QWidget::destroyed, this, &Import_TSX::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Import_TSX::StopThread);// , Qt::QueuedConnection);
    connect(import_TSX_thread, &MyThread::sendModel, this, &Import_TSX::TransitModel);
    import_TSX_thread->thread()->start();
    emit operate(
        ui->comboBox_pol->currentText(),
        ui->LineEdit_xml->text(),
        this->save_path,
        ui->lineEdit_dst_node->text(),
        ui->LineEdit_dst_filename->text(),
        ui->comboBox_dst_project->currentText(),
        this->copy
    );
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("导入图像文件为空！"));
        return;
    }
    //检查目标节点名
    if (ui->lineEdit_dst_node_2->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点名为空！"));
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    //根据原始文件日期生成导入文件名称
    vector<QString> original_namelist;
    vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist)) return;


    import_TSX_thread2 = new MyThread;
    import_TSX_thread2->moveToThread(new QThread(this));
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &Import_TSX::operate2, import_TSX_thread2, &MyThread::import_TSX_patch, Qt::QueuedConnection);
    connect(import_TSX_thread2, &MyThread::updateProcess, this, &Import_TSX::updateProcess);
    connect(import_TSX_thread2->thread(), &QThread::finished, import_TSX_thread2, &MyThread::deleteLater);
    connect(import_TSX_thread2, &MyThread::endProcess, this, &Import_TSX::endProcess);
    connect(this, &QWidget::destroyed, this, &Import_TSX::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &Import_TSX::StopThread);// , Qt::QueuedConnection);
    connect(import_TSX_thread2, &MyThread::sendModel, this, &Import_TSX::TransitModel);
    import_TSX_thread2->thread()->start();
    emit operate2(
        ui->comboBox_pol2->currentText(),
        this->save_path, //保存路径
        original_namelist,//原始文件名
        import_namelist, //导入文件名
        ui->lineEdit_dst_node_2->text(), //导入节点名
        ui->comboBox_dst_project_2->currentText(), //导入工程名
        this->copy);
    ChangeVision(false);
}

void Import_TSX::on_buttonBox_2_rejected()
{
    close();
}
