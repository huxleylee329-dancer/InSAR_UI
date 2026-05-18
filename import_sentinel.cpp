#include"MainWindow.h"
#include"import_sentinel.h"
#include"icon_source.h"
#include"qfiledialog.h"
#include<opencv2/highgui.hpp>
#include<qmessagebox.h>
//#include<FormatConversion.h>
//#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
//#pragma comment(lib, "FormatConversion_d.lib")
//#endif
import_sentinel::import_sentinel(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportSentinel)
{
    ui->setupUi(this);
    import_sentinel_thread = NULL;
    import_sentinel_thread_2 = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);

    subswath = "iw1"; polarization = "vv";
    ui->ComboBox_subswath->addItem("iw1");
    ui->ComboBox_subswath->addItem("iw2");
    ui->ComboBox_subswath->addItem("iw3");
    ui->ComboBox_subswath_2->addItem("iw1");
    ui->ComboBox_subswath_2->addItem("iw2");
    ui->ComboBox_subswath_2->addItem("iw3");
    ui->ComboBox_subswath->setCurrentIndex(0);
    ui->ComboBox_polarization->addItem("vv");
    ui->ComboBox_polarization->addItem("vh");
    ui->ComboBox_polarization->setCurrentIndex(0);
    ui->ComboBox_polarization_2->addItem("vv");
    ui->ComboBox_polarization_2->addItem("vh");
    ui->ComboBox_polarization_2->setCurrentIndex(0);
    ui->lineEdit_dst_node->setPlaceholderText(QString::fromLocal8Bit("不要输入中文字符"));
    ui->lineEdit_dst_node_2->setPlaceholderText(QString::fromLocal8Bit("不要输入中文字符"));
    old_path = "C:\\";
    date = "";
}
import_sentinel::~import_sentinel()
{
    import_sentinel_thread = NULL;
    import_sentinel_thread_2 = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->ComboBox_dst_project_2->count(); i++)
        {
            if (!copy->findItems(ui->ComboBox_dst_project_2->itemText(i)).isEmpty())
                copy->findItems(ui->ComboBox_dst_project_2->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

bool import_sentinel::generate_name(QListWidget* imageslist, vector<QString>& original_nameslist, vector<QString>& import_nameslist)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        original_nameslist.push_back(imageslist->item(i)->text());
        QFileInfo fileinfo = QFileInfo(imageslist->item(i)->text());
        QString tmp = fileinfo.baseName();
        QString filename = imageslist->item(i)->text();
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(filename.toStdString().c_str());
        if (ret < 0)
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        TiXmlElement* root = NULL, * pnode = NULL;
        ret = xmldoc.find_node("dataObjectSection", root);
        if (ret < 0)
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        ret = xmldoc._find_node(root, "dataObject", pnode);
        if (ret < 0)
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        string tmp1(pnode->FirstAttribute()->Value());
        if (tmp1.length() > 18)
        {
            date = QString(tmp1.substr(18, 8).c_str());
            QString name = date + "_" + subswath + polarization;
            import_nameslist.push_back(name);
        }
        else
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        root = NULL; pnode = NULL;
    }
    return true;
}

void import_sentinel::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->ComboBox_dst_project_2->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->lineEdit_dst_node_2->setDisabled(0);
        ui->LineEdit_dst_filename->setDisabled(0);
        ui->lineEdit_manifest_file->setDisabled(0);
        ui->lineEdit_POD->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
        ui->pushButton_POD->setDisabled(0);
        ui->browse_Button->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
        ui->ComboBox_polarization->setDisabled(0);
        ui->ComboBox_polarization_2->setDisabled(0);
        ui->ComboBox_subswath->setDisabled(0);
        ui->ComboBox_subswath_2->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->ComboBox_dst_project_2->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->lineEdit_dst_node_2->setDisabled(1);
        ui->LineEdit_dst_filename->setDisabled(1);
        ui->lineEdit_manifest_file->setDisabled(1);
        ui->lineEdit_POD->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
        ui->pushButton_POD->setDisabled(1);
        ui->browse_Button->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
        ui->ComboBox_polarization->setDisabled(1);
        ui->ComboBox_polarization_2->setDisabled(1);
        ui->ComboBox_subswath->setDisabled(1);
        ui->ComboBox_subswath_2->setDisabled(1);
    }
}


void import_sentinel::updateProcess(int value, QString information)
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
void import_sentinel::endProcess()
{
    if (import_sentinel_thread)
    {
        import_sentinel_thread->thread()->quit();
        import_sentinel_thread->thread()->wait();
        
    }
    if (import_sentinel_thread_2)
    {
        import_sentinel_thread_2->thread()->quit();
        import_sentinel_thread_2->thread()->wait();

    }
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    this->close();
}
void import_sentinel::endThread()
{
    if (import_sentinel_thread)
    {
        import_sentinel_thread->thread()->quit();
        import_sentinel_thread->thread()->wait();
    }
    if (import_sentinel_thread_2)
    {
        import_sentinel_thread_2->thread()->quit();
        import_sentinel_thread_2->thread()->wait();
    }
    
}
void import_sentinel::StopThread()
{
    if (import_sentinel_thread != NULL)
        if (import_sentinel_thread->thread()->isRunning())
        {
            import_sentinel_thread->thread()->requestInterruption();
            import_sentinel_thread->thread()->quit();
            import_sentinel_thread->thread()->wait();
        }
    if (import_sentinel_thread_2 != NULL)
        if (import_sentinel_thread_2->thread()->isRunning())
        {
            import_sentinel_thread_2->thread()->requestInterruption();
            import_sentinel_thread_2->thread()->quit();
            import_sentinel_thread_2->thread()->wait();
        }

}
void import_sentinel::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_sentinel::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}
void import_sentinel::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i, 0)->text());
        ui->ComboBox_dst_project_2->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    ui->comboBox_dst_project->setCurrentIndex(0);
    ui->ComboBox_dst_project_2->setCurrentIndex(0);
    this->save_path = model->item(0, 1)->text();
}
void import_sentinel::on_ComboBox_dst_project_2_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->ComboBox_dst_project_2->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}
void import_sentinel::on_pushButton_add_pressed()
{
    bool isWarning = 0;
    QString dirname = QFileDialog::getExistingDirectory(this,
        QString::fromLocal8Bit("导入哨兵数据"),
        "/",
        QFileDialog::ShowDirsOnly);
    QDir* FileDir = new QDir(dirname);
    QStringList filter;
    filter << "S1A_*";
    FileDir->setNameFilters(filter);
    QList<QFileInfo>* Dirinfo = new QList<QFileInfo>(FileDir->entryInfoList(filter));
    int count = Dirinfo->count();
    //ui->listWidget->addItem(filename);
    for (int i = 0; i < count; i++)
    {
        QString filename = "";
        if(Dirinfo->at(i).fileName().contains(".SAFE", Qt::CaseSensitive))
        {
            filename = Dirinfo->at(i).filePath() + "/manifest.safe";
        }
        else
        {
            filename = Dirinfo->at(i).filePath() + "/" + Dirinfo->at(i).baseName() + ".SAFE/manifest.safe";
        }
        if (!filename.isEmpty())
        {
            bool isRepeated = 0;
            for (int j = 0; j < ui->listWidget->count(); j++)
            {
                if (filename == ui->listWidget->item(j)->text())
                {
                    isRepeated = 1;
                    isWarning = 1;
                    break;
                }
            }
            if (!isRepeated)
                ui->listWidget->addItem(filename);

        }
    }
    if (isWarning)
    {
        QMessageBox::warning(NULL, QString::fromLocal8Bit("注意"), QString::fromLocal8Bit("检测到您试图重复添加相同数据，已将其忽略。"));
    }
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
            ui->listWidget->takeItem(i);
    }
}
void import_sentinel::on_pushButton_remove_pressed()
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
void import_sentinel::on_browse_Button_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "Open sentinel manifest.safe file",
        this->old_path,
        "file(*.safe)");
    if (QFile::exists(filename))
    {
        ui->lineEdit_manifest_file->setText(filename);
        //QFileInfo fileinfo = QFileInfo(filename);
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(filename.toStdString().c_str());
        if (ret < 0)
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            return;
        }
        TiXmlElement* root = NULL, * pnode = NULL;
        ret = xmldoc.find_node("dataObjectSection", root);
        if (ret < 0)
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            return;
        }
        ret = xmldoc._find_node(root, "dataObject", pnode);
        if (ret < 0)
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            return;
        }
        string tmp(pnode->FirstAttribute()->Value());
        if (tmp.length() > 18)
        {
            date = QString(tmp.substr(18, 8).c_str());
            ui->LineEdit_dst_filename->setText(date + QString("_") + ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            
        }
        else
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
        }
    }
}

void import_sentinel::on_pushButton_POD_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "Open sentinel POD file",
        this->old_path,
        "file(*.EOF)");
    if (QFile::exists(filename))
    {
        ui->lineEdit_POD->setText(filename);        
    }
}

void import_sentinel::on_buttonBox_2_accepted()
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
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dst_node_2->text() == project->child(i)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    //根据原始文件名及极化方式和子带生成导入文件名称
    vector<QString> original_namelist;
    vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist)) return;

    // Ensure previous threads are released
    if (import_sentinel_thread_2) {
        import_sentinel_thread_2->thread()->quit();
        import_sentinel_thread_2->thread()->wait();
    }

    import_sentinel_thread_2 = new MyThread;
    QThread* thread2 = new QThread(this);
    import_sentinel_thread_2->moveToThread(thread2);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &import_sentinel::operate2, import_sentinel_thread_2, &MyThread::import_sentinel_patch, Qt::QueuedConnection);
    connect(import_sentinel_thread_2, &MyThread::updateProcess, this, &import_sentinel::updateProcess);
    connect(thread2, &QThread::finished, import_sentinel_thread_2, &MyThread::deleteLater);
    connect(thread2, &QThread::finished, thread2, &QThread::deleteLater);
    connect(import_sentinel_thread_2, &MyThread::endProcess, this, &import_sentinel::endProcess);
    connect(this, &QWidget::destroyed, this, &import_sentinel::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &import_sentinel::StopThread);// , Qt::QueuedConnection);
    connect(import_sentinel_thread_2, &MyThread::sendModel, this, &import_sentinel::TransitModel);
    thread2->start();
    emit operate2(
        original_namelist,
        import_namelist,
        subswath,
        polarization,
        this->save_path,
        ui->lineEdit_dst_node_2->text(),
        ui->ComboBox_dst_project_2->currentText(),
        this->copy
    );
    ChangeVision(false);
}

void import_sentinel::on_buttonBox_accepted()
{
    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点为空！"));
        return;
    }
    if (ui->lineEdit_manifest_file->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("输入图像文件为空！"));
        return;
    }
    if (ui->LineEdit_dst_filename->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("导入图像文件名为空！"));
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

    // Ensure previous threads are released
    if (import_sentinel_thread) {
        import_sentinel_thread->thread()->quit();
        import_sentinel_thread->thread()->wait();
    }

    import_sentinel_thread = new MyThread;
    QThread* thread = new QThread(this);
    import_sentinel_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &import_sentinel::operate, import_sentinel_thread, &MyThread::import_sentinel, Qt::QueuedConnection);
    connect(import_sentinel_thread, &MyThread::updateProcess, this, &import_sentinel::updateProcess);
    connect(thread, &QThread::finished, import_sentinel_thread, &MyThread::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_sentinel_thread, &MyThread::endProcess, this, &import_sentinel::endProcess);
    connect(this, &QWidget::destroyed, this, &import_sentinel::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_sentinel::StopThread);// , Qt::QueuedConnection);
    connect(import_sentinel_thread, &MyThread::sendModel, this, &import_sentinel::TransitModel);
    thread->start();
    emit operate(
        ui->lineEdit_POD->text(),
        ui->lineEdit_manifest_file->text(), 
        ui->ComboBox_subswath->currentText(),
        ui->ComboBox_polarization->currentText(), 
        this->save_path, 
        ui->lineEdit_dst_node->text(),
        ui->LineEdit_dst_filename->text(),
        ui->comboBox_dst_project->currentText(),
        this->copy
    );
    ChangeVision(false);
    
}
void import_sentinel::on_buttonBox_2_rejected()
{
    close();
}
void import_sentinel::on_buttonBox_rejected()
{
    close();
}

void import_sentinel::on_ComboBox_subswath_currentIndexChanged()
{
    if (date.length() > 0)
    {
        ui->LineEdit_dst_filename->setText(date + QString("_") + ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
    else
    {
        ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
    
}

void import_sentinel::on_ComboBox_subswath_2_currentIndexChanged()
{
    if (ui->ComboBox_subswath_2->count() > 0)
    {
        if (ui->ComboBox_subswath_2->currentIndex() == 0) subswath = "iw1";
        else if (ui->ComboBox_subswath_2->currentIndex() == 1) subswath = "iw2";
        else subswath = "iw3";
    }
}

void import_sentinel::on_ComboBox_polarization_currentIndexChanged()
{
    if (date.length() > 0)
    {
        ui->LineEdit_dst_filename->setText(date + QString("_") + ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
    else
    {
        ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
}

void import_sentinel::on_ComboBox_polarization_2_currentIndexChanged()
{
    if (ui->ComboBox_polarization_2->count() > 0)
    {
        if (ui->ComboBox_polarization_2->currentIndex() == 0) polarization = "vv";
        else polarization = "vh";
    }
}
