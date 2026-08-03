#include "SLC_deramp.h"
#include "ui_SlcDeramp.h"
#include "SLCDerampWorker.h"
#include <QThread>

#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QFileInfo>
#include<QDir>
#include"FormatConversion.h"
#include "NodeUtils.h"
#include "tinyxml.h"
#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#endif
SLC_deramp::SLC_deramp(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::SlcDeramp)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();

}
SLC_deramp::~SLC_deramp()
{
    if (copy)
    {
        if (copy->findItems(ui->comboBox->currentText())[0])
            copy->findItems(ui->comboBox->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    }
    emit sendCopy(copy);
    m_thread = nullptr;
    m_worker = nullptr;
}

void SLC_deramp::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void SLC_deramp::endProcess()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    ui->progressBar->hide();
    this->close();
}
void SLC_deramp::endThread()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}
void SLC_deramp::StopThread()
{
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}
void SLC_deramp::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
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
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        ui->comboBox_dst_node->clear();
        //ui->comboBox_masterImage->clear();
        return;
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    ui->comboBox_dst_node->clear();
    for (int i = 0; i < count; i++)
    {
        if (project->child(i, 1)->text() == QString("complex-2.0"))
        {
            ui->comboBox_dst_node->addItem(project->child(i, 0)->text());
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
        ui->comboBox_dst_node->clear();
        //ui->comboBox_masterImage->clear();
        return;
    }
    ui->comboBox_dst_node->setCurrentIndex(0);

    //ui->comboBox_masterImage->clear();
    //for (int i = 0; i < node->rowCount(); i++)
    //{
    //    ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
    //}
    //if (ui->comboBox_masterImage->count() < 1)
    //{
    //    QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
    //    ui->comboBox_masterImage->clear();
    //    return;
    //}
    //ui->comboBox_masterImage->setCurrentIndex(0);

}
void SLC_deramp::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        bool isnodefound = false;
        QStandardItem* node = NULL;
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        ui->comboBox_dst_node->clear();
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 1)->text() == QString("complex-2.0"))
            {
                ui->comboBox_dst_node->addItem(project->child(i, 0)->text());
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
            ui->comboBox_dst_node->clear();
            //ui->comboBox_masterImage->clear();
            return;
        }
        //ui->comboBox_masterImage->clear();
        //for (int i = 0; i < node->rowCount(); i++)
        //{
        //    ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
        //}
        //if (ui->comboBox_masterImage->count() < 1)
        //{
        //    QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
        //    ui->comboBox_masterImage->clear();
        //    return;
        //}
        //ui->comboBox_masterImage->setCurrentIndex(0);
    }
}



void SLC_deramp::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_dst_node->setDisabled(0);
        //ui->comboBox_masterImage->setDisabled(0);
        ui->lineEdit->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_dst_node->setDisabled(1);
        //ui->comboBox_masterImage->setDisabled(1);
        ui->lineEdit->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        
    }
}


void SLC_deramp::on_comboBox_dst_node_currentIndexChanged()
{
   /* if (ui->comboBox_dst_node->count() > 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QString temp = project->child(i, 0)->text();
            if (project->child(i, 0)->text() == ui->comboBox_dst_node->currentText())
            {
                node = project->child(i, 0); break;
            }
        }

        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            ui->comboBox_masterImage->clear();
            return;
        }
        ui->comboBox_masterImage->clear();
        int count = node->rowCount();
        for (int i = 0; i < count; i++)
        {
            ui->comboBox_masterImage->addItem(node->child(i, 0)->text());
        }
        ui->comboBox_masterImage->setCurrentIndex(0);
    }*/
}

void SLC_deramp::on_buttonBox_accepted()
{
    XMLFile xmldoc;
    TiXmlElement* pnode = NULL, * pchild = NULL;
    bool bFlag = false;
    if (copy->item(ui->comboBox->currentIndex(), 0)->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }

    //确定主图像序号
    QString projectFile;
    for (int i = 0; i < copy->rowCount(); i++)
    {
        if (copy->item(i, 0)->text() == ui->comboBox->currentText())
        {
            projectFile = copy->item(i, 1)->text() + "/" + copy->item(i, 0)->text();
            break;
        }
    }
    if (projectFile.isEmpty()) return;
    int ret = xmldoc.XMLFile_load(projectFile.toStdString().c_str());
    if (ret < 0) return;
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        if (pnode->Attribute("name") == ui->comboBox_dst_node->currentText()) break;
        pnode = pnode->NextSiblingElement();
    }
    if (!pnode) return;
    ret = xmldoc._find_node(pnode, "master_image", pchild);
    if (ret < 0) return;
    int index = 1;
    ret = sscanf(pchild->GetText(), "%d", &index);
    if (ret != 1 || index < 1) return;

    QStandardItem* inputNode = nullptr;
    for (int i = 0; i < project->rowCount(); ++i)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == ui->comboBox_dst_node->currentText())
        {
            inputNode = node;
            break;
        }
    }
    if (!inputNode)
        return;

    QStringList inputPaths;
    for (int i = 0; i < inputNode->rowCount(); ++i)
    {
        QStandardItem* pathItem = inputNode->child(i, 1);
        if (pathItem && !pathItem->text().isEmpty())
            inputPaths.append(pathItem->text());
    }
    if (inputPaths.isEmpty() || index > inputPaths.size())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("Selected input image file was not found."));
        return;
    }

    auto* iface = NodeUtils::getProjectContext(this);
    const QString demPath = iface ? NodeUtils::getGlobalDemPath(iface) : QString();
    if (demPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("请先配置可用的 DEM 数据。"));
        return;
    }

    m_masterIndex = index;
    m_thread = new QThread(this);
    m_worker = new SLCDerampWorker();
    m_worker->moveToThread(m_thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &SLC_deramp::operate, m_worker, &SLCDerampWorker::SLC_deramp, Qt::QueuedConnection);
    connect(m_worker, &SLCDerampWorker::updateProcess, this, &SLC_deramp::updateProcess);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SLCDerampWorker::endProcess, this, &SLC_deramp::endProcess);
    connect(this, &QWidget::destroyed, this, &SLC_deramp::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SLC_deramp::StopThread);
    connect(m_worker, &SLCDerampWorker::sendResults, this, &SLC_deramp::handleResults);
    m_thread->start();
    ChangeVision(false);
    emit operate(index, ui->comboBox->currentText(), save_path, ui->lineEdit->text(), inputPaths, demPath);


}

void SLC_deramp::on_buttonBox_rejected()
{
    this->close();
}

void SLC_deramp::handleResults(
    const QString& dstNode,
    const QStringList& h5Paths,
    const QStringList& originNames,
    const QString& savePath,
    const QString& projectName)
{
    if (h5Paths.isEmpty() || h5Paths.size() != originNames.size())
        return;

    const QList<QStandardItem*> projects = copy ? copy->findItems(projectName) : QList<QStandardItem*>();
    if (!projects.isEmpty()) {
        QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
            projects.first(), dstNode, "complex-3.0", FOLDER_ICON);
        if (outputNode) {
            outputNode->setToolTip(projectName);
            for (const QString& h5Path : h5Paths) {
                const QString outputName = QFileInfo(h5Path).baseName();
                QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
                    outputNode, outputName, "complex", h5Path, IMAGEDATA_ICON);
                if (imageItem) {
                    outputNode->setChild(imageItem->row(), 1, new QStandardItem(h5Path));
                }
            }
        }
    }

    XMLFile xml;
    const QString xmlPath = savePath + "/" + projectName;
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) == 0) {
        TiXmlElement* root = nullptr;
        xml.get_root(root);
        if (root) {
            TiXmlElement* dataNode = nullptr;
            for (TiXmlElement* item = root->FirstChildElement(); item; item = item->NextSiblingElement()) {
                const char* name = item->Attribute("name");
                if (name && strcmp(item->Value(), "DataNode") == 0 && QString(name) == dstNode) {
                    dataNode = item;
                    break;
                }
            }
            if (!dataNode) {
                dataNode = new TiXmlElement("DataNode");
                dataNode->SetAttribute("name", dstNode.toStdString().c_str());
                dataNode->SetAttribute("data_count", "0");
                dataNode->SetAttribute("data_processing", "SLC_deramp");
                dataNode->SetAttribute("rank", "complex-3.0");
                root->LinkEndChild(dataNode);
            }

            int dataCount = 0;
            for (TiXmlElement* item = dataNode->FirstChildElement("Data"); item; item = item->NextSiblingElement("Data")) {
                ++dataCount;
            }
            for (int i = 0; i < h5Paths.size(); ++i) {
                const QFileInfo fileInfo(h5Paths.at(i));
                const QString outputName = originNames.at(i) + "_deramp";
                bool exists = false;
                for (TiXmlElement* item = dataNode->FirstChildElement("Data"); item; item = item->NextSiblingElement("Data")) {
                    TiXmlElement* nameItem = item->FirstChildElement("Data_Name");
                    if (nameItem && nameItem->GetText() && QString(nameItem->GetText()) == outputName) {
                        exists = true;
                        break;
                    }
                }
                if (exists)
                    continue;

                ++dataCount;
                TiXmlElement* data = new TiXmlElement("Data");
                TiXmlElement* name = new TiXmlElement("Data_Name");
                name->LinkEndChild(new TiXmlText(outputName.toStdString().c_str()));
                data->LinkEndChild(name);
                TiXmlElement* rank = new TiXmlElement("Data_Rank");
                rank->LinkEndChild(new TiXmlText("complex-3.0"));
                data->LinkEndChild(rank);
                TiXmlElement* dataIndex = new TiXmlElement("Data_Index");
                dataIndex->LinkEndChild(new TiXmlText(QString::number(dataCount).toStdString().c_str()));
                data->LinkEndChild(dataIndex);
                TiXmlElement* path = new TiXmlElement("Data_Path");
                path->LinkEndChild(new TiXmlText(QString("/%1/%2").arg(dstNode, fileInfo.fileName()).toStdString().c_str()));
                data->LinkEndChild(path);
                dataNode->LinkEndChild(data);
            }
            dataNode->SetAttribute("data_count", QString::number(dataCount).toStdString().c_str());
            xml.XMLFile_save(xmlPath.toStdString().c_str());
        }
    }
}
