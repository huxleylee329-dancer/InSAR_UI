#include "UnwrapWorker.h"
#include "NodeUtils.h"
#include <Unwrap.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QStandardItem>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Unwrap_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Unwrap.lib")
#endif

using namespace cv;
using namespace std;

UnwrapWorker::UnwrapWorker(QObject* parent)
    : QObject(parent)
{
}

UnwrapWorker::~UnwrapWorker()
{
}

void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("UnwrapWorker", QString("Unwrap task started. Output folder: %1, Method: %2").arg(file_name).arg(method));

    if (save_path.isEmpty() ||
        project_name.isEmpty() ||
        node_name.isEmpty() ||
        file_name.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }

    QDir dir(save_path);
    QString absolute_path;
    if (!dir.exists(file_name))
    {
        dir.mkdir(file_name);
        absolute_path = save_path + "/" + file_name;
    }
    else
    {
        dir.remove(file_name);
        dir.mkdir(file_name);
        absolute_path = save_path + "/" + file_name;
    }

    QList<QStandardItem*> foundProjects = model->findItems(project_name);
    if (foundProjects.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到对应的工程: ") + project_name);
        return;
    }

    QStandardItem* project = foundProjects.first();
    QStandardItem* node = NULL;
    QList<QString> phase_name;
    QList<QString> phase_path;
    QList<QString> unwrap_name;
    QList<QString> relative_unwrap_path;
    QList<QString> absolute_unwrap_path;

    emit updateProcess(10, QStringLiteral("准备数据……"));
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == node_name)
        {
            node = project->child(i, 0);
            for (int j = 0; j < node->rowCount(); j++)
            {
                if (node->child(j, 0)->toolTip() == "phase")
                {
                    QString change_name;
                    QString origin_name = node->child(j, 0)->text();
                    phase_name.append(node->child(j, 0)->text());
                    phase_path.append(node->child(j, 1)->text());
                    change_name = origin_name + "_unwrapped";
                    unwrap_name.append(change_name);
                    relative_unwrap_path.append("/" + file_name + "/" + change_name + ".h5");
                    absolute_unwrap_path.append(save_path + "/" + file_name + "/" + change_name + ".h5");
                }
            }
            break;
        }
    }

    if (!node) {
        emit errorProcess(QStringLiteral("未找到指定的数据节点: ") + node_name);
        return;
    }

    /*建立根节点*/
    QStandardItem* Unwrap_node = NULL;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == file_name)
        {
            Unwrap_node = project->child(i, 0);
            break;
        }
    }

    if (!Unwrap_node)
    {
        Unwrap_node = new QStandardItem(file_name);
        Unwrap_node->setToolTip(project_name);
        int insert = 0;
        for (; insert < project->rowCount(); insert++)
        {
            if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-3.0") == 0)
                continue;
            else
                break;
        }
        Unwrap_node->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, Unwrap_node);
        QStandardItem* Unwrap_node_Rank = new QStandardItem("phase-3.0");
        project->setChild(insert, 1, Unwrap_node_Rank);
    }

    int image_number = phase_name.size();
    if (image_number == 0) {
        emit errorProcess(QStringLiteral("没有可解缠的干涉图像"));
        return;
    }

    ::Unwrap unwrap;
    FormatConversion FC;
    Utils util;
    XMLFile* xml = new XMLFile();
    
    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) {
        xml_path += ".Insar";
    }

    if (xml->XMLFile_load(xml_path.toStdString().c_str()) < 0) {
        delete xml;
        emit errorProcess(QStringLiteral("加载项目XML文件失败: ") + xml_path);
        return;
    }

    int ret = 0;

    auto copyMetadata = [&](int idx, QString method_str) -> bool {
        /*写入h5*/
        ret = FC.creat_new_h5(absolute_unwrap_path.at(idx).toStdString().c_str());
        if (ret < 0) return false;

        string tmp_str;
        Mat tmp;
        ret = FC.read_str_from_h5(phase_path.at(idx).toStdString().c_str(), "source_1", tmp_str);
        ret = FC.write_str_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "source_1", tmp_str.c_str());
        QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());

        ret = FC.read_str_from_h5(phase_path.at(idx).toStdString().c_str(), "source_2", tmp_str);
        ret = FC.write_str_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "source_2", tmp_str.c_str());
        QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());

        ret = FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "flat_phase_coefficient", tmp);
        ret = FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "flat_phase_coefficient", tmp);

        ret = FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "range_len", tmp);
        ret = FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "range_len", tmp);

        ret = FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "azimuth_len", tmp);
        ret = FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "azimuth_len", tmp);

        FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "multilook_rg", tmp);
        FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "multilook_rg", tmp);

        FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "multilook_az", tmp);
        FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "multilook_az", tmp);

        if (0 == FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "mapped_lon", tmp))
            FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "mapped_lon", tmp);
        if (0 == FC.read_array_from_h5(phase_path.at(idx).toStdString().c_str(), "mapped_lat", tmp))
            FC.write_array_to_h5(absolute_unwrap_path.at(idx).toStdString().c_str(), "mapped_lat", tmp);

        if (QThread::currentThread()->isInterruptionRequested())
        {
            return false;
        }

        /*行列偏移量*/
        Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
        ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
        int offset_row = tmp_int.at<int>(0, 0);
        ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
        int offset_col = tmp_int.at<int>(0, 0);

        xml->XMLFile_add_unwrap(file_name.toStdString().c_str(), unwrap_name.at(idx).toStdString().c_str(),
            relative_unwrap_path.at(idx).toStdString().c_str(), offset_row, offset_col, method_str.toStdString().c_str(), 0);

        /*工程树*/
        QStandardItem* item_img = NULL;
        for (int j = 0; j < Unwrap_node->rowCount(); j++)
        {
            if (Unwrap_node->child(j, 0)->text() == unwrap_name.at(idx))
            {
                item_img = Unwrap_node->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* image = new QStandardItem(unwrap_name.at(idx));
            image->setToolTip("phase");
            image->setIcon(QIcon(IMAGEDATA_ICON));
            Unwrap_node->appendRow(image);
            QStandardItem* image_path = new QStandardItem(absolute_unwrap_path.at(idx));
            Unwrap_node->setChild(Unwrap_node->rowCount() - 1, 1, image_path);
        }
        else
        {
            Unwrap_node->setChild(item_img->row(), 1, new QStandardItem(absolute_unwrap_path.at(idx)));
        }

        return true;
    };

    if (method == 1)
    {
        for (int i = 0; i < image_number; i++)
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                delete xml;
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) continue;
            
            Mat phase_unwrap;
            ret = unwrap.SPD_Guided_Unwrap(phase, phase_unwrap);
            if (ret < 0) continue;

            if (!copyMetadata(i, "SPD_Guided")) {
                delete xml;
                return;
            }
            ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
        }
    }
    else if (method == 2)
    {
        for (int i = 0; i < image_number; i++)
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                delete xml;
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) continue;

            Mat phase_unwrap;
            Mat coherence, residue;
            ret = util.phase_coherence(phase, coherence);
            ret = util.residue(phase, residue);
            QString app_path = QCoreApplication::applicationDirPath();
            ret = unwrap.MCF(phase, phase_unwrap, coherence, residue, (absolute_path + "/MCF.net").toStdString().c_str(), app_path.toStdString().c_str());
            if (ret < 0) continue;

            if (!copyMetadata(i, "MCF")) {
                delete xml;
                return;
            }
            ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
        }
    }
    else if (method == 3)
    {
        for (int i = 0; i < image_number; i++)
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                delete xml;
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) continue;

            Mat phase_unwrap;
            QString app_path = QCoreApplication::applicationDirPath();
            ret = unwrap.snaphu(phase_path.at(i).toStdString().c_str(), phase_unwrap, save_path.toStdString().c_str(), absolute_path.toStdString().c_str(), app_path.toStdString().c_str());
            if (ret < 0) continue;

            if (!copyMetadata(i, "Snaphu")) {
                delete xml;
                return;
            }
            ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
        }
    }
    else if (method == 4)
    {
        double distance_threshold = 5.0;
        for (int i = 0; i < image_number; i++)
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                delete xml;
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) continue;

            Mat phase_unwrap;
            QString app_path = QCoreApplication::applicationDirPath();
            ret = unwrap.QualityGuided_MCF(phase, phase_unwrap, coherence_threshold, distance_threshold, absolute_path.toStdString().c_str(), app_path.toStdString().c_str());
            if (ret < 0) continue;

            if (!copyMetadata(i, "QualityGuided_MCF")) {
                delete xml;
                return;
            }
            ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
        }
    }

    xml->XMLFile_save(xml_path.toStdString().c_str());
    delete xml;

    emit sendModel(model);
    InSARLogManager::LogInfo("UnwrapWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}
