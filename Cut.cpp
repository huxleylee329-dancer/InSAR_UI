#include"Cut.h"
#include"NodeUtils.h"
#include"tinyxml.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<Utils.h>
#include <QThread>
#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#endif
#include<FormatConversion.h>
Cut::Cut(QWidget* parent) :
    QWidget(parent), h5_left(-1), h5_right(-1), h5_top(-1), h5_bottom(-1),
    ui(new Ui::Cut)
{
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    //this->DOC = new XMLFile;
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setHidden(1);
    isPreviewPressed = false;
    isCutting = false;
    if (copy != NULL)
    {    
        copy = NULL;
    }

}
Cut::~Cut()
{
    Cut_thread = NULL;
    /*改变工程文件的处理状态为NOT_IN_PROCESS*/
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->comboBox_3->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_3->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_3->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}
void Cut::updateProcess(int value, QString information)
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
void Cut::endProcess()
{
    Cut_thread->thread()->quit();
    Cut_thread->thread()->wait();
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    isPreviewPressed = false;
    isCutting = false;
    ui->Preview->setDisabled(false);
    ui->Preview->setText(QStringLiteral("预览"));
    ui->Preview->repaint();
    ui->buttonBox_2->setDisabled(false);
    ui->comboBox_3->setDisabled(false);
    ui->comboBox_4->setDisabled(false);
    ui->lineEdit_2->setDisabled(false);
    this->close();
}
void Cut::endThread()
{
    Cut_thread->thread()->quit();
    Cut_thread->thread()->wait();
}
void Cut::StopThread()
{
    if (Cut_thread != NULL)
    {
        if (Cut_thread->thread()->isRunning())
        {
            Cut_thread->thread()->requestInterruption();
            Cut_thread->thread()->quit();
            Cut_thread->thread()->wait();
        }
    }
    isCutting = false;
}
void Cut::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}
void Cut::ReceivePos(double left, double right, double top, double bottom)
{
    if (ui->comboBox_4->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据，请先配准再进行框选裁剪或更换工程！"));
        return;
    }
    if (ui->lineEdit_2->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入存放裁剪文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->lineEdit_2->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }

    QString project_name = ui->comboBox_3->currentText();
    QString src_node = ui->comboBox_4->currentText();
    QString dst_node = ui->lineEdit_2->text();

    QList<QStandardItem*> foundProjects = copy->findItems(project_name);
    if (foundProjects.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未在项目中查找到该工程！"));
        return;
    }
    QStandardItem* project = foundProjects[0];
    QStandardItem* node = nullptr;
    int src_node_index = -1;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == src_node)
        {
            node = project->child(i, 0);
            src_node_index = i;
            break;
        }
    }

    if (src_node_index < 0 || !node)
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未在工程中查找到源数据节点！"));
        return;
    }

    QStringList inputPaths;
    for (int i = 0; i < node->rowCount(); i++)
    {
        inputPaths.append(node->child(i, 1)->text());
    }

    if (inputPaths.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("源节点不包含任何可处理数据文件！"));
        return;
    }

    // 所有前置校验通过后，才修改 isCutting 和 UI 状态
    isCutting = true;
    h5_left = left;
    h5_right = right;
    h5_top = top;
    h5_bottom = bottom;

    ui->Preview->setText(QStringLiteral("正在裁剪..."));
    ui->Preview->repaint();
    ui->buttonBox_2->setDisabled(true);
    ui->comboBox_3->setDisabled(true);
    ui->comboBox_4->setDisabled(true);
    ui->lineEdit_2->setDisabled(true);
    ui->progressBar_2->setHidden(0);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();

    Cut_thread = new CutWorker;
    Cut_thread->moveToThread(new QThread(this));

    QString src_data_rank = (project->child(src_node_index, 1)) ? project->child(src_node_index, 1)->text() : QString("complex-1.0");
    QStandardItem* Images_Cut = NodeUtils::findOrCreateProjectNode(project, dst_node, src_data_rank);
    if (Images_Cut)
    {
        Images_Cut->setToolTip(project_name);
    }

    QByteArray file_abs_path = QString("%1/%2").arg(this->save_path).arg(project_name.endsWith(".insar", Qt::CaseInsensitive) ? project_name : project_name + ".insar").toLocal8Bit();
    auto doc = std::make_shared<XMLFile>();
    doc->XMLFile_load(file_abs_path.data());

    int master_index = -1;
    TiXmlElement* DataNode = nullptr;
    int ret = doc->find_node_with_attribute("DataNode", "name", src_node.toStdString().c_str(), DataNode);
    if (ret == 0 && DataNode)
    {
        TiXmlElement* pnode = nullptr;
        ret = doc->_find_node(DataNode, "master_image", pnode);
        if (ret == 0 && pnode)
        {
            ret = sscanf(pnode->GetText(), "%d", &master_index);
            if (ret != 1) master_index = -1;
        }
    }

    connect(Cut_thread, &CutWorker::updateProcess, this, &Cut::updateProcess);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread, &QObject::deleteLater);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread->thread(), &QObject::deleteLater);

    // 连接 fileCropped 信号以安全地在 GUI 线程更新树模型和 XML 内存
    connect(Cut_thread, &CutWorker::fileCropped, this, [=](QString cutName, QString fullPath, int offsetRow, int offsetCol, int masterIdx, QString rank, QList<double> cPara) {
        Q_UNUSED(cPara);
        if (!Images_Cut) return;
        QStandardItem* item_img = nullptr;
        for (int j = 0; j < Images_Cut->rowCount(); j++)
        {
            if (Images_Cut->child(j, 0)->text() == cutName)
            {
                item_img = Images_Cut->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* Image_Cut_Name = new QStandardItem(cutName);
            QStandardItem* Image_Cut_Path = new QStandardItem(fullPath);
            Image_Cut_Name->setIcon(QIcon(IMAGEDATA_ICON));
            Images_Cut->appendRow(Image_Cut_Name);
            Image_Cut_Name->setToolTip("complex");
            Images_Cut->setChild(Images_Cut->rowCount() - 1, 1, Image_Cut_Path);

            QByteArray dir_name = dst_node.toLocal8Bit();
            QByteArray filename = cutName.toLocal8Bit();
            QByteArray file_relative_path = QString("/%1/%2.h5").arg(dst_node).arg(cutName).toLocal8Bit();
            doc->XMLFile_add_cut(dir_name.data(), masterIdx, filename.data(),
                file_relative_path.data(),
                offsetRow, offsetCol, 0, 0, 0, 0, rank.toStdString().c_str());
        }
        else
        {
            Images_Cut->setChild(item_img->row(), 1, new QStandardItem(fullPath));
        }
    }, Qt::QueuedConnection);

    // 连接 endProcess 信号以安全保存 XML
    connect(Cut_thread, &CutWorker::endProcess, this, [=]() {
        doc->XMLFile_save(file_abs_path.data());
        this->endProcess();
    }, Qt::QueuedConnection);

    connect(this, &QWidget::destroyed, this, &Cut::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Cut::StopThread);
    
    Cut_thread->thread()->start();
    ChangeVision(false);
    
    QMetaObject::invokeMethod(Cut_thread, [=]() {
        Cut_thread->Cut2(h5_left, h5_right, h5_top, h5_bottom,
                         this->save_path,
                         project_name.endsWith(".insar", Qt::CaseInsensitive) ? project_name : project_name + ".insar",
                         src_node, dst_node, inputPaths, src_data_rank, master_index);
    }, Qt::QueuedConnection);
}

void Cut::cancelled()
{
    if (isCutting) return;//由于预览子窗口在点击确定或者关闭之后都会发送destroy消息，为了区分，设置是否正在裁剪标志。
    isPreviewPressed = false;
    ui->Preview->setDisabled(false);
    ui->Preview->setText(QStringLiteral("预览"));
    ui->Preview->repaint();
}

void Cut::ShowProjectList(QStandardItemModel *model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i,0)->text());
        ui->comboBox_3->addItem(model->item(i, 0)->text());
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
            ui->comboBox->setCurrentIndex(i);
            ui->comboBox_3->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    ui->comboBox_4->clear();
    for (int i = 0; i < count; i++)
    {
        if(model->data(model->index(i, 1, pro_index)).toString().compare("complex-0.0")==0)
            ui->comboBox_2->addItem(model->data(model->index(i,0,pro_index)).toString());
        if( model->data(model->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
            )
            ui->comboBox_4->addItem(model->data(model->index(i, 0, pro_index)).toString());
        
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据，请先导入图像数据！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);

}
void Cut::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        //this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_2->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0)
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }

}

void Cut::writeXML()
{
    
}

void Cut::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->lineEdit->setDisabled(0);
        ui->lon->setDisabled(0);
        ui->lat->setDisabled(0);
        ui->Width->setDisabled(0);
        ui->Height->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->lineEdit->setDisabled(1);
        ui->lon->setDisabled(1);
        ui->lat->setDisabled(1);
        ui->Width->setDisabled(1);
        ui->Height->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void Cut::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i,0)->text() == ui->comboBox_2->currentText())
                image_number = project->child(i, 0)->rowCount();
        }
    }
    
}

void Cut::on_comboBox_3_currentIndexChanged()
{
    if (ui->comboBox_3->count() != 0)
    {
        //this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_4->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
                )
                ui->comboBox_4->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Cut::on_comboBox_4_currentIndexChanged()
{
    if (ui->comboBox_4->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_4->currentText())
                image_number = project->child(i, 0)->rowCount();
        }
    }

}

void Cut::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据，请先导入数据或更换工程！"));
        return;
    }
    if (ui->lineEdit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入存放裁剪文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->lineEdit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    if (ui->lon->text().isEmpty() || ui->lat->text().isEmpty() ||
        ui->Height->text().isEmpty() || ui->Width->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请确保输入参数完整！"));
        return;
    }
    ui->lon->text().toDouble(&bFlag);
    bool bFlag2 = ui->lat->text().toDouble(&bFlag2);
    if (bFlag ==false || bFlag2 == false)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("经纬度应为小数！"));
        return;
    }
    ui->Height->text().toDouble(&bFlag);
    ui->Width->text().toDouble(&bFlag2);
    if (bFlag == false || bFlag2 == false)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("高度与宽度应为小数！"));
        return;
    }
    QString project_name = ui->comboBox->currentText();
    QString src_node = ui->comboBox_2->currentText();
    QString dst_node = ui->lineEdit->text();

    QList<QStandardItem*> foundProjects = copy->findItems(project_name);
    if (foundProjects.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未在项目中查找到该工程！"));
        return;
    }
    QStandardItem* project = foundProjects[0];
    QStandardItem* node = nullptr;
    int src_node_index = -1;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == src_node)
        {
            node = project->child(i, 0);
            src_node_index = i;
            break;
        }
    }

    if (src_node_index < 0 || !node)
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未在工程中查找到源数据节点！"));
        return;
    }

    QStringList inputPaths;
    for (int i = 0; i < node->rowCount(); i++)
    {
        inputPaths.append(node->child(i, 1)->text());
    }

    if (inputPaths.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("源节点不包含任何可处理数据文件！"));
        return;
    }

    ui->progressBar->setHidden(0);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setValue(0);
    ui->progressBar->show();

    Cut_thread = new CutWorker;
    Cut_thread->moveToThread(new QThread(this));

    QList<double> para;
    para.push_back(ui->lon->text().toDouble());
    para.push_back(ui->lat->text().toDouble());
    para.push_back(ui->Width->text().toDouble());
    para.push_back(ui->Height->text().toDouble());

    QStandardItem* Images_Cut = NodeUtils::findOrCreateProjectNode(project, dst_node, "complex-1.0");
    if (Images_Cut)
    {
        Images_Cut->setToolTip(project_name);
    }

    QByteArray file_abs_path = QString("%1/%2").arg(this->save_path).arg(project_name.endsWith(".insar", Qt::CaseInsensitive) ? project_name : project_name + ".insar").toLocal8Bit();
    auto doc = std::make_shared<XMLFile>();
    doc->XMLFile_load(file_abs_path.data());

    connect(Cut_thread, &CutWorker::updateProcess, this, &Cut::updateProcess);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread, &QObject::deleteLater);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread->thread(), &QObject::deleteLater);

    // 连接 fileCropped 信号以安全地在 GUI 线程更新树模型和 XML 内存
    connect(Cut_thread, &CutWorker::fileCropped, this, [=](QString cutName, QString fullPath, int offsetRow, int offsetCol, int masterIdx, QString rank, QList<double> cPara) {
        Q_UNUSED(masterIdx);
        Q_UNUSED(rank);
        if (!Images_Cut) return;
        QStandardItem* item_img = nullptr;
        for (int j = 0; j < Images_Cut->rowCount(); j++)
        {
            if (Images_Cut->child(j, 0)->text() == cutName)
            {
                item_img = Images_Cut->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* Image_Cut_Name = new QStandardItem(cutName);
            QStandardItem* Image_Cut_Path = new QStandardItem(fullPath);
            Image_Cut_Name->setIcon(QIcon(IMAGEDATA_ICON));
            Images_Cut->appendRow(Image_Cut_Name);
            Image_Cut_Name->setToolTip("complex");
            Images_Cut->setChild(Images_Cut->rowCount() - 1, 1, Image_Cut_Path);

            QByteArray dir_name = dst_node.toLocal8Bit();
            QByteArray filename = cutName.toLocal8Bit();
            QByteArray file_relative_path = QString("/%1/%2.h5").arg(dst_node).arg(cutName).toLocal8Bit();
            doc->XMLFile_add_cut(dir_name.data(), -1, filename.data(),
                file_relative_path.data(),
                offsetRow, offsetCol, cPara.at(0), cPara.at(1),
                cPara.at(2), cPara.at(3), "complex-1.0");
        }
        else
        {
            Images_Cut->setChild(item_img->row(), 1, new QStandardItem(fullPath));
        }
    }, Qt::QueuedConnection);

    // 连接 endProcess 信号以安全保存 XML
    connect(Cut_thread, &CutWorker::endProcess, this, [=]() {
        doc->XMLFile_save(file_abs_path.data());
        this->endProcess();
    }, Qt::QueuedConnection);

    connect(this, &QWidget::destroyed, this, &Cut::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Cut::StopThread);
    
    Cut_thread->thread()->start();
    ChangeVision(false);
    
    QMetaObject::invokeMethod(Cut_thread, [=]() {
        Cut_thread->Cut(para, this->save_path,
                        project_name.endsWith(".insar", Qt::CaseInsensitive) ? project_name : project_name + ".insar",
                        src_node, dst_node, inputPaths, QString("complex-1.0"));
    }, Qt::QueuedConnection);

    
}

void Cut::on_buttonBox_rejected()
{
    this->close();
}

void Cut::on_Preview_pressed()
{
    if (isPreviewPressed) return;
    isPreviewPressed = true;
    ui->Preview->setDisabled(true);
    ui->Preview->setText(QStringLiteral("正在加载预览图..."));
    ui->Preview->repaint();
    QStandardItem* project = copy->findItems(ui->comboBox_3->currentText())[0];
    QString image_name;
    QString image_path;
    QString jpg_path;
    QFileInfo fileinfo;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_4->currentText())
        {
            image_name = project->child(i, 0)->child(0, 0)->text();
            image_path = project->child(i, 0)->child(0, 1)->text();
            fileinfo = QFileInfo(image_path);
            jpg_path = QString("%1%2%3%4").arg(fileinfo.absolutePath()).arg("/").arg(image_name).arg(".jpg");
            break;
        }
    }
    if (QFile::exists(jpg_path))
    {

    }
    else
    {
        Utils util;
        ComplexMat SLC64;
        FormatConversion FC;
        FC.read_slc_from_h5(image_path.toStdString().c_str(), SLC64);
        util.SAR_image_quantify(jpg_path.toStdString().c_str(), /*65*/65, SLC64);
        if (QThread::currentThread()->isInterruptionRequested())
        {
            //emit endProcess();
            QFile::remove(jpg_path.toStdString().c_str());
            return;
        }
        if (SLC64.GetCols() * SLC64.GetRows() > 25e6)
        {
            int down_sample_times = (int)sqrt(floor(double(SLC64.GetCols() * SLC64.GetRows()) / 25e6));
            util.resampling(jpg_path.toStdString().c_str(), jpg_path.toStdString().c_str(), (int)(SLC64.GetRows() / down_sample_times),
                (int)(SLC64.GetCols() / down_sample_times));
        }
    }
    
    Preview_Window* Pre_wnd = new Preview_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    connect(Pre_wnd->View, &Preview::SendPos, this, &Cut::ReceivePos);
    connect(Pre_wnd, &Preview::close, this, &Cut::cancelled);
    connect(Pre_wnd, &Preview::destroyed, this, &Cut::cancelled);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}

void Cut::on_buttonBox_2_accepted()
{
    QMessageBox::warning(NULL, "Warning!", QStringLiteral("请预览并选择裁剪区域！"));
}

void Cut::on_buttonBox_2_rejected()
{
    this->close();
}
