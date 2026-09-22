#include"S1_swath_merge.h"
#include"ui_S1SwathMerge.h"
#include"icon_source.h"
#include "NodeUtils.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QFileInfo>
#include<QDir>
#include "FormatConversion.h"

S1_swath_merge::S1_swath_merge(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1SwathMerge),
    copy(nullptr),
    S1_swath_merge_worker(nullptr),
    S1_swath_merge_thread(nullptr)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
}
S1_swath_merge::~S1_swath_merge()
{
    StopThread();
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_project->count(); i++)
        {
            QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->itemText(i));
            if (project)
                project->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    S1_swath_merge_thread = nullptr;
    S1_swath_merge_worker = nullptr;
    delete ui;
    ui = nullptr;
}

void S1_swath_merge::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void S1_swath_merge::endProcess()
{
    if (S1_swath_merge_thread)
    {
        S1_swath_merge_thread->quit();
        S1_swath_merge_thread->wait();
        S1_swath_merge_thread = nullptr;
    }
    S1_swath_merge_worker = nullptr;
    ui->progressBar->hide();
    this->close();
}
void S1_swath_merge::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (S1_swath_merge_thread)
    {
        S1_swath_merge_thread->quit();
        S1_swath_merge_thread->wait();
        S1_swath_merge_thread = nullptr;
    }
    S1_swath_merge_worker = nullptr;
    ui->progressBar->hide();
    ChangeVision(true);
}
void S1_swath_merge::endThread()
{
    if (S1_swath_merge_thread)
    {
        S1_swath_merge_thread->quit();
        S1_swath_merge_thread->wait();
        S1_swath_merge_thread = nullptr;
    }
    S1_swath_merge_worker = nullptr;
}
void S1_swath_merge::StopThread()
{
    if (S1_swath_merge_worker)
        S1_swath_merge_worker->StopProcess();

    QThread* workerThread = S1_swath_merge_thread.data();
    if (workerThread && workerThread->isRunning())
    {
        workerThread->requestInterruption();
        workerThread->quit();
        if (QThread::currentThread() != workerThread)
            workerThread->wait();
    }
    S1_swath_merge_thread = nullptr;
    S1_swath_merge_worker = nullptr;
}
void S1_swath_merge::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project->setDisabled(0);
        ui->comboBox_IW1_node->setDisabled(0);
        ui->comboBox_IW2_node->setDisabled(0);
        ui->comboBox_IW3_node->setDisabled(0);
        ui->comboBox_IW1_data->setDisabled(0);
        ui->comboBox_IW2_data->setDisabled(0);
        ui->comboBox_IW3_data->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->lineEdit_dstNode->setDisabled(0);
    }
    else
    {
        ui->comboBox_project->setDisabled(1);
        ui->comboBox_IW1_node->setDisabled(1);
        ui->comboBox_IW2_node->setDisabled(1);
        ui->comboBox_IW3_node->setDisabled(1);
        ui->comboBox_IW1_data->setDisabled(1);
        ui->comboBox_IW2_data->setDisabled(1);
        ui->comboBox_IW3_data->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->lineEdit_dstNode->setDisabled(1);
    }


}

void S1_swath_merge::ShowProjectList(QStandardItemModel* model)
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
        ui->comboBox_project->addItem(projectItem->text());
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
            ui->comboBox_project->setCurrentIndex(ui->comboBox_project->findText(projectItem->text()));
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_IW1_node->clear();
    ui->comboBox_IW2_node->clear();
    ui->comboBox_IW3_node->clear();
    bool isnodefound = false;
    QStandardItem* node = NULL, * imagedata = NULL;
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0)
        {
            QStandardItem* nameItem = project->child(i, 0);
            if (!nameItem) continue;
            ui->comboBox_IW1_node->addItem(nameItem->text());
            ui->comboBox_IW2_node->addItem(nameItem->text());
            ui->comboBox_IW3_node->addItem(nameItem->text());
            if (!isnodefound)
            {
                node = nameItem;
                isnodefound = true;
            }
        }
    }
    if (!isnodefound)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
        //this->deleteLater();
        return;
    }
    ui->comboBox_IW1_node->setCurrentIndex(0);
    ui->comboBox_IW2_node->setCurrentIndex(0);
    ui->comboBox_IW3_node->setCurrentIndex(0);

    ui->comboBox_IW1_data->clear();
    ui->comboBox_IW2_data->clear();
    ui->comboBox_IW3_data->clear();

    //初始化图像数据节点
    if (node->rowCount() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("节点无数据！"));
        //this->deleteLater();
        return;
    }
    for (int i = 0; i < node->rowCount(); i++)
    {
        QStandardItem* imageItem = node->child(i, 0);
        if (!imageItem) continue;
        ui->comboBox_IW1_data->addItem(imageItem->text());
        ui->comboBox_IW2_data->addItem(imageItem->text());
        ui->comboBox_IW3_data->addItem(imageItem->text());
    }
    ui->comboBox_IW1_data->setCurrentIndex(0);
    ui->comboBox_IW2_data->setCurrentIndex(0);
    ui->comboBox_IW3_data->setCurrentIndex(0);
}

void S1_swath_merge::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
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
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
            //this->deleteLater();
            return;
        }
        QStandardItem* node = NULL;
        ui->comboBox_IW1_node->clear();
        ui->comboBox_IW2_node->clear();
        ui->comboBox_IW3_node->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0)
            {
                ui->comboBox_IW1_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                ui->comboBox_IW2_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                ui->comboBox_IW3_node->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无满足需求的数据！"));
            //this->deleteLater();
            return;
        }
        ui->comboBox_IW1_node->setCurrentIndex(0);
        ui->comboBox_IW2_node->setCurrentIndex(0);
        ui->comboBox_IW3_node->setCurrentIndex(0);

        
        //初始化图像数据节点
        ui->comboBox_IW1_data->clear();
        ui->comboBox_IW2_data->clear();
        ui->comboBox_IW3_data->clear();
        if (node->rowCount() < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < node->rowCount(); i++)
        {
            QStandardItem* imageItem = node->child(i, 0);
            if (!imageItem) continue;
            ui->comboBox_IW1_data->addItem(imageItem->text());
            ui->comboBox_IW2_data->addItem(imageItem->text());
            ui->comboBox_IW3_data->addItem(imageItem->text());
        }
        ui->comboBox_IW1_data->setCurrentIndex(0);
        ui->comboBox_IW2_data->setCurrentIndex(0);
        ui->comboBox_IW3_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_comboBox_IW1_node_currentIndexChanged()
{
    if (ui->comboBox_IW1_node->count() != 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* childItem = project->child(i, 0);
            if (childItem && childItem->text() == ui->comboBox_IW1_node->currentText())
            {
                num = childItem->rowCount(); node = childItem; break;
            }
        }
        ui->comboBox_IW1_data->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            QStandardItem* childItem = node->child(i, 0);
            if (childItem)
                ui->comboBox_IW1_data->addItem(childItem->text());
        }
        ui->comboBox_IW1_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_comboBox_IW2_node_currentIndexChanged()
{
    if (ui->comboBox_IW2_node->count() != 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* childItem = project->child(i, 0);
            if (childItem && childItem->text() == ui->comboBox_IW2_node->currentText())
            {
                num = childItem->rowCount(); node = childItem; break;
            }
        }
        ui->comboBox_IW2_data->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            QStandardItem* childItem = node->child(i, 0);
            if (childItem)
                ui->comboBox_IW2_data->addItem(childItem->text());
        }
        ui->comboBox_IW2_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_comboBox_IW3_node_currentIndexChanged()
{
    if (ui->comboBox_IW3_node->count() != 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* node = NULL;
        int num = 0;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* childItem = project->child(i, 0);
            if (childItem && childItem->text() == ui->comboBox_IW3_node->currentText())
            {
                num = childItem->rowCount(); node = childItem; break;
            }
        }
        ui->comboBox_IW3_data->clear();
        if (!node || num <= 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            //this->deleteLater();
            return;
        }
        for (int i = 0; i < num; i++)
        {
            QStandardItem* childItem = node->child(i, 0);
            if (childItem)
                ui->comboBox_IW3_data->addItem(childItem->text());
        }
        ui->comboBox_IW3_data->setCurrentIndex(0);
    }
}

void S1_swath_merge::on_buttonBox_accepted()
{
    if (ui->comboBox_IW1_node->count() == 0 || ui->comboBox_IW2_node->count() == 0 || ui->comboBox_IW3_node->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstNode->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("名称应当为数字、字母及下划线的组合！"));
        return;
    }
    //防重名检查
    QStandardItem* project = NodeUtils::findFirstModelItem(this->copy, ui->comboBox_project->currentText());
    if (!project) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* childItem = project->child(i, 0);
        if (childItem && ui->lineEdit_dstNode->text() == childItem->text())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }


    const auto selectedPath = [project](const QString& nodeName, int dataIndex) -> QString {
        if (dataIndex < 0)
            return QString();
        for (int i = 0; i < project->rowCount(); ++i)
        {
            QStandardItem* node = project->child(i, 0);
            if (!node || node->text() != nodeName || node->rowCount() <= dataIndex)
                continue;
            QStandardItem* pathItem = node->child(dataIndex, 1);
            return pathItem ? pathItem->text() : QString();
        }
        return QString();
    };
    const QString firstH5Path = selectedPath(
        ui->comboBox_IW1_node->currentText(), ui->comboBox_IW1_data->currentIndex());
    const QString secondH5Path = selectedPath(
        ui->comboBox_IW2_node->currentText(), ui->comboBox_IW2_data->currentIndex());
    const QString thirdH5Path = selectedPath(
        ui->comboBox_IW3_node->currentText(), ui->comboBox_IW3_data->currentIndex());
    if (firstH5Path.isEmpty() || secondH5Path.isEmpty() || thirdH5Path.isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("Selected input image file was not found."));
        return;
    }

    S1_swath_merge_worker = new S1SwathMergeWorker();
    S1_swath_merge_thread = new QThread(this);
    S1_swath_merge_worker->moveToThread(S1_swath_merge_thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_swath_merge::operate, S1_swath_merge_worker, &S1SwathMergeWorker::S1_swath_merge, Qt::QueuedConnection);
    connect(S1_swath_merge_worker, &S1SwathMergeWorker::updateProcess, this, &S1_swath_merge::updateProcess);
    connect(S1_swath_merge_thread, &QThread::finished, S1_swath_merge_worker, &QObject::deleteLater);
    connect(S1_swath_merge_thread, &QThread::finished, S1_swath_merge_thread, &QObject::deleteLater);
    connect(S1_swath_merge_worker, &S1SwathMergeWorker::endProcess, this, &S1_swath_merge::endProcess);
    connect(S1_swath_merge_worker, &S1SwathMergeWorker::errorProcess, this, &S1_swath_merge::errorProcess);
    connect(this, &QWidget::destroyed, this, &S1_swath_merge::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_swath_merge::StopThread);
    connect(S1_swath_merge_worker, &S1SwathMergeWorker::sendResult, this, &S1_swath_merge::handleResult);
    S1_swath_merge_thread->start();
    ChangeVision(false);
    operate(
        ui->comboBox_project->currentText(),
        save_path,
        ui->lineEdit_dstNode->text(),
        firstH5Path,
        secondH5Path,
        thirdH5Path
    );
}

void S1_swath_merge::on_buttonBox_rejected()
{
    this->close();
}

void S1_swath_merge::handleResult(
    const QString& dstNode,
    const QString& filename,
    const QString& mergedH5Path,
    const QString& savePath,
    const QString& projectName)
{
    if (savePath.isEmpty() || projectName.isEmpty())
        return;

    QString xml_path = savePath + "/" + projectName;
    QString relative_path = "/" + dstNode + "/" + QFileInfo(mergedH5Path).fileName();

    const QList<QStandardItem*> projects = copy ? copy->findItems(projectName) : QList<QStandardItem*>();
    if (!projects.isEmpty()) {
        QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
            projects.first(), dstNode, "phase-1.0", FOLDER_ICON);
        if (outputNode) {
            outputNode->setToolTip(projectName);
            QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
                outputNode, filename, "phase", mergedH5Path, IMAGEDATA_ICON);
            if (imageItem) {
                outputNode->setChild(imageItem->row(), 1, new QStandardItem(mergedH5Path));
            }
        }
    }

    XMLFile xml;
    if (xml.XMLFile_load(xml_path.toStdString().c_str()) == 0)
    {
        xml.XMLFile_add_interferometric_phase(dstNode.toStdString().c_str(), filename.toStdString().c_str(),
            relative_path.toStdString().c_str(), "unknown", "phase-1.0", 0, 0, 0, 0, 0, 0, 0, 0, 0);
        xml.XMLFile_save(xml_path.toStdString().c_str());
    }
}
