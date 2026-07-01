#include "SLCDerampWorker.h"
#include <FormatConversion.h>
#include <Deflat.h>
#include <Utils.h>
#include <Package.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QCoreApplication>
#include <QStandardItem>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "Deflat_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Filter_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "Deflat.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Filter.lib")
#endif

using namespace cv;
using namespace std;

thread_local SLCDerampWorker* t_currentDerampWorker = nullptr;
thread_local int t_derampLastLoggedProgress = -10;

static bool __stdcall derampProgressCallback(int progress, const char* message)
{
    if (t_currentDerampWorker)
    {
        if (t_currentDerampWorker->thread()->isInterruptionRequested() || t_currentDerampWorker->isStopRequested())
        {
            return false;
        }

        int start_prog = 10;
        int end_prog = 50;
        int mapped_prog = start_prog + progress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_currentDerampWorker->updateProcess(mapped_prog, QStringLiteral("去参考相位 - DEM映射中：%1% (%2)")
            .arg(progress).arg(msgStr));

        if (progress == 0 || progress == 100 || (progress - t_derampLastLoggedProgress) >= 10)
        {
            InSARLogManager::LogInfo("SLCDerampWorker", QString("demMapping progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
            t_derampLastLoggedProgress = progress;
        }
    }
    return true;
}

struct DerampThreadLocalGuard {
    DerampThreadLocalGuard(SLCDerampWorker* worker) {
        t_currentDerampWorker = worker;
        t_derampLastLoggedProgress = -10;
    }
    ~DerampThreadLocalGuard() {
        t_currentDerampWorker = nullptr;
        t_derampLastLoggedProgress = -10;
    }
};

SLCDerampWorker::SLCDerampWorker(QObject* parent)
    : BaseWorker(parent)
{
}

SLCDerampWorker::~SLCDerampWorker()
{
}

void SLCDerampWorker::SLC_deramp(
    int masterIndex,
    QString project_name,
    QString src_node,
    QString dst_node,
    QStandardItemModel* model
)
{
    NodeUtils::Hdf5Locker locker;
    DerampThreadLocalGuard tlGuard(this);
    InSARLogManager::LogInfo("SLCDerampWorker", QString("SLC_deramp task started. Source: %1, Destination: %2").arg(src_node).arg(dst_node));

    if (masterIndex < 1 ||
        project_name.isEmpty() ||
        dst_node.isEmpty() ||
        src_node.isEmpty() ||
        !model
        )
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    // 确定外部 DEM 文件夹
    QString appPath = QCoreApplication::applicationDirPath();
    QString demPath = appPath + "/dem";
    QDir appDir(appPath);
    if (!appDir.exists("dem")) appDir.mkdir("dem");

    // 确定待处理数据文件
    Utils util; FormatConversion conversion; Deflat flat;
    vector<string> SAR_images, SAR_images_deramp;
    QList<QString> origin;

    QString save_path;
    int image_number = 0;
    bool found_project = false;
    bool found_image = false;

    QMetaObject::invokeMethod(model, [=, &SAR_images, &SAR_images_deramp, &origin, &save_path, &image_number, &found_project, &found_image]() {
        QList<QStandardItem*> foundProjects = model->findItems(project_name);
        if (foundProjects.isEmpty()) return;
        found_project = true;
        QStandardItem* project = foundProjects.first();
        QStandardItem* image = NULL;
        save_path = model->item(project->row(), 1)->text();
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == src_node)
            {
                image = project->child(i, 0);
                break;
            }
        }
        if (!image) return;
        found_image = true;

        image_number = image->rowCount();
        for (int i = 0; i < image->rowCount(); i++)
        {
            SAR_images.push_back(image->child(i, 1)->text().toStdString());
            QFileInfo fileinfo(image->child(i, 1)->text());
            QString origin_name = fileinfo.baseName();
            origin.append(origin_name);
            SAR_images_deramp.push_back(QString("%1/%2/%3_deramp.h5").arg(save_path).arg(dst_node)
                .arg(origin_name).toStdString());
        }
    }, Qt::BlockingQueuedConnection);

    if (!found_project) {
        emit errorProcess(QStringLiteral("未找到对应的工程: ") + project_name);
        return;
    }
    if (!found_image) {
        emit errorProcess(QStringLiteral("在项目中未找到输入数据节点: ") + src_node);
        return;
    }
    if (SAR_images.empty()) {
        emit errorProcess(QStringLiteral("输入节点下没有发现可处理的数据文件"));
        return;
    }

    if (masterIndex > (int)SAR_images.size()) {
        emit errorProcess(QStringLiteral("主图像索引越界，可用图像数: ") + QString::number(SAR_images.size()));
        return;
    }

    emit updateProcess(10, QStringLiteral("开始计算……"));
    int ret;
    QDir dir(save_path);
    if (!dir.exists(dst_node)) dir.mkdir(dst_node);
    for (int i = 0; i < image_number; i++)
    {
        ret = conversion.creat_new_h5(SAR_images_deramp[i].c_str());
    }

    double lonMax, lonMin, latMax, latMin, lon_upperleft, lat_upperleft, rangeSpacing,
        nearRangeTime, wavelength, prf, start, end;
    int sceneHeight, sceneWidth, offset_row, offset_col;
    Mat lon_coef, lat_coef, dem, mappedDem, statevec;
    ComplexMat slc;
    string start_time, end_time, master_file;
    master_file = SAR_images[masterIndex - 1];

    ret = conversion.read_int_from_h5(master_file.c_str(), "range_len", &sceneWidth);
    ret = conversion.read_int_from_h5(master_file.c_str(), "azimuth_len", &sceneHeight);
    ret = conversion.read_int_from_h5(master_file.c_str(), "offset_row", &offset_row);
    ret = conversion.read_int_from_h5(master_file.c_str(), "offset_col", &offset_col);
    ret = conversion.read_array_from_h5(master_file.c_str(), "lon_coefficient", lon_coef);
    ret = conversion.read_array_from_h5(master_file.c_str(), "lat_coefficient", lat_coef);
    ret = conversion.read_double_from_h5(master_file.c_str(), "prf", &prf);
    ret = conversion.read_double_from_h5(master_file.c_str(), "carrier_frequency", &wavelength);
    wavelength = VEL_C / wavelength;
    ret = conversion.read_double_from_h5(master_file.c_str(), "range_spacing", &rangeSpacing);
    ret = conversion.read_double_from_h5(master_file.c_str(), "slant_range_first_pixel", &nearRangeTime);
    nearRangeTime = 2.0 * nearRangeTime / VEL_C;
    ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_start_time", start_time);
    ret = conversion.utc2gps(start_time.c_str(), &start);
    ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_stop_time", end_time);
    ret = conversion.utc2gps(end_time.c_str(), &end);
    ret = conversion.read_array_from_h5(master_file.c_str(), "state_vec", statevec);
    
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        emit errorProcess(QStringLiteral("用户取消操作"));
        return;
    }

    ret = Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
        &lonMax, &latMax, &lonMin, &latMin);
    ret = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem, &lon_upperleft, &lat_upperleft, lonMin, lonMax, latMin, latMax);
    Mat mappedLon, mappedLat;
    ret = flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lon_upperleft, lat_upperleft, offset_row, offset_col, sceneHeight, sceneWidth,
        prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec, 20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, derampProgressCallback);



    ret = conversion.write_array_to_h5(SAR_images_deramp[masterIndex - 1].c_str(), "mapped_lat", mappedLat);
    ret = conversion.write_array_to_h5(SAR_images_deramp[masterIndex - 1].c_str(), "mapped_lon", mappedLon);

    QStringList resultH5Paths;
    QStringList resultOriginNames;

    for (int i = 0; i < image_number; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            emit errorProcess(QStringLiteral("用户取消操作"));
            return;
        }

        ret = flat.SLC_deramp(slc, mappedDem, mappedLat, mappedLon, SAR_images[i].c_str());
        ret = conversion.write_slc_to_h5(SAR_images_deramp[i].c_str(), slc);
        ret = conversion.Copy_para_from_h5_2_h5(SAR_images[i].c_str(), SAR_images_deramp[i].c_str());
        ret = conversion.read_int_from_h5(SAR_images[i].c_str(), "offset_row", &offset_row);
        ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "offset_row", offset_row);
        ret = conversion.read_int_from_h5(SAR_images[i].c_str(), "offset_col", &offset_col);
        ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "offset_col", offset_col);
        ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "range_len", sceneWidth);
        ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "azimuth_len", sceneHeight);

        double process = 10.0 + 80.0 / (double(image_number)) * double(i + 1);

        QFileInfo fileinfo = QFileInfo(QString(SAR_images_deramp.at(i).c_str()));
        QString deramp_name = fileinfo.baseName();
        resultH5Paths.append(fileinfo.absoluteFilePath());
        resultOriginNames.append(origin.at(i));

        // 拼接成功后，立刻生成 JPG 预览图（在后台线程中执行）
        QString h5Path = fileinfo.absoluteFilePath();
        QString jpgPath = fileinfo.absolutePath() + "/" + fileinfo.baseName() + ".jpg";
        NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "complex");

        emit updateProcess(process, QStringLiteral("进度..."));
    }

    QMetaObject::invokeMethod(model, [=]() {
        QList<QStandardItem*> foundProjects = model->findItems(project_name);
        if (foundProjects.isEmpty()) return;
        QStandardItem* project = foundProjects.first();

        QStandardItem* deramp = NULL;
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == dst_node)
            {
                deramp = project->child(i, 0);
                break;
            }
        }

        if (!deramp)
        {
            deramp = new QStandardItem(dst_node);
            deramp->setToolTip(project_name);
            int insert = 0;
            for (; insert < project->rowCount(); insert++)
            {
                if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                    project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                    project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
                    project->child(insert, 1)->text().compare("complex-3.0") == 0
                    )
                    continue;
                else
                    break;
            }
            deramp->setIcon(QIcon(FOLDER_ICON));
            project->insertRow(insert, deramp);
            QStandardItem* deramp_Rank = new QStandardItem("complex-3.0");
            project->setChild(insert, 1, deramp_Rank);
        }

        for (int i = 0; i < image_number; i++)
        {
            QFileInfo fileinfo = QFileInfo(QString(SAR_images_deramp.at(i).c_str()));
            QString deramp_name = fileinfo.baseName();
            QStandardItem* item_img = NULL;
            for (int j = 0; j < deramp->rowCount(); j++)
            {
                if (deramp->child(j, 0)->text() == deramp_name)
                {
                    item_img = deramp->child(j, 0);
                    break;
                }
            }

            if (!item_img)
            {
                QStandardItem* deramp_images_name = new QStandardItem(deramp_name);
                deramp_images_name->setToolTip("complex");
                QStandardItem* deramp_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                deramp_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                deramp->appendRow(deramp_images_name);
                deramp->setChild(deramp->rowCount() - 1, 1, deramp_images_path);
            }
            else
            {
                deramp->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
            }
        }
    }, Qt::BlockingQueuedConnection);

    emit sendModel(model);
    // 回传数据到 Node 模块，用于落盘自愈
    emit sendResults(dst_node, resultH5Paths, resultOriginNames);

    InSARLogManager::LogInfo("SLCDerampWorker", QString("SLC_deramp completed. Total images: %1").arg(image_number));
    emit endProcess();
}
