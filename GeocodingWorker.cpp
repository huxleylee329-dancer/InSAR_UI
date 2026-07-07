#include "GeocodingWorker.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "Package.h"
#include <FormatConversion.h>
#include "tinyxml.h"
#include <Utils.h>
#include <Deflat.h>
#include <Filter.h>
#include <Registration.h>
#include <Unwrap.h>
#include <Dem.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include <QElapsedTimer>
#include "InSARLogManager.h"

using namespace cv;

thread_local GeocodingWorker* t_currentGeocodingWorker = nullptr;
thread_local int t_geocodingLastLoggedProgress = -10;

static bool __stdcall geocodingProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer s_cbTimer;
    thread_local bool s_timerStarted = false;
    if (!s_timerStarted) {
        s_cbTimer.start();
        s_timerStarted = true;
    }
    if (progress != 0 && progress != 100 && s_cbTimer.elapsed() < 100) {
        return true;
    }
    s_cbTimer.restart();

    if (t_currentGeocodingWorker)
    {
        if (t_currentGeocodingWorker->thread()->isInterruptionRequested() || t_currentGeocodingWorker->isStopRequested())
        {
            return false;
        }

        int start_prog = 2;
        int end_prog = 20;
        int mapped_prog = start_prog + progress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_currentGeocodingWorker->updateProcess(mapped_prog, QStringLiteral("正在地理编码：%1% (%2)")
            .arg(progress).arg(msgStr));

        if (progress == 0 || progress == 100 || (progress - t_geocodingLastLoggedProgress) >= 10)
        {
            InSARLogManager::LogInfo("GeocodingWorker", QString("demMapping progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
            t_geocodingLastLoggedProgress = progress;
        }
    }
    return true;
}

struct GeocodingThreadLocalGuard {
    GeocodingThreadLocalGuard(GeocodingWorker* worker) {
        t_currentGeocodingWorker = worker;
        t_geocodingLastLoggedProgress = -10;
    }
    ~GeocodingThreadLocalGuard() {
        t_currentGeocodingWorker = nullptr;
        t_geocodingLastLoggedProgress = -10;
    }
};

GeocodingWorker::GeocodingWorker(QObject* parent)
    : BaseWorker(parent)
{
}

GeocodingWorker::~GeocodingWorker()
{
}

void GeocodingWorker::Geocoding(
    int type,
    int multi_rg,
    int multi_az,
    QString project_name,
    QString srcNode,
    QString dstNode,
    QStandardItemModel* model
)
{
    GeocodingWithDem(type, multi_rg, multi_az, project_name, srcNode, dstNode, model, QString());
}

void GeocodingWorker::GeocodingWithDem(
    int type,
    int multi_rg,
    int multi_az,
    QString project_name,
    QString srcNode,
    QString dstNode,
    QStandardItemModel* model,
    QString dem_path
)
{
    GeocodingThreadLocalGuard guard(this);
    if (!model) {
        emit errorProcess(QStringLiteral("模型指针为空！"));
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        emit errorProcess(QStringLiteral("任务已被中止。"));
        return;
    }

    QStandardItem* project = model->findItems(project_name)[0];
    if (!project) {
        emit errorProcess(QStringLiteral("未找到工程节点！"));
        return;
    }
    QString save_path = model->item(project->row(), 1)->text();
    QDir dir(save_path);
    if (!dir.exists(dstNode))
        dir.mkdir(dstNode);
    //外部DEM文件夹或文件路径
    QString demPath = dem_path;
    if (demPath.isEmpty() || QFileInfo(demPath).isFile()) {
        QString appPath = QCoreApplication::applicationDirPath();
        demPath = appPath + "/dem";
        QDir appDir(appPath);
        if (!appDir.exists("dem")) appDir.mkdir("dem");
    }

    std::vector<std::string> input_files;
    std::vector<std::string> output_files;
    QList<QString> origin;
    QString product_level;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* images = project->child(i, 0);
        if (images->text() == srcNode)
        {
            product_level = project->child(i, 1)->text();
            for (int j = 0; j < images->rowCount(); j++)
            {
                QFileInfo fileinfo(images->child(j, 1)->text());
                QString origin_name = fileinfo.baseName();
                origin.append(origin_name);
                input_files.push_back(images->child(j, 1)->text().toStdString());
                output_files.push_back(QString("%1/%2/%3_geocoded.h5").arg(save_path).arg(dstNode)
                    .arg(origin_name).toStdString());
            }
            break;
        }
    }

    if (input_files.empty()) {
        emit errorProcess(QStringLiteral("未找到输入文件！"));
        return;
    }

    emit updateProcess(2, QStringLiteral("正在地理编码……"));
    FormatConversion conversion; Utils util;
    QString geocode_Rank_level;
    int ret;
    //干涉产品地理编码
    if (type == 1)
    {
        std::string source_file;
        Mat mapped_lat, mapped_lon, phase, mapped_phase;
        double lonMax = 0, lonMin = 0, latMax = 0, latMin = 0, lon_upperleft = 0, lat_upperleft = 0, rangeSpacing = 0,
            nearRangeTime = 0, wavelength = 0, prf = 0, start = 0, end = 0;
        int sceneHeight = 0, sceneWidth = 0, offset_row = 0, offset_col = 0, multilook_rg = 1, multilook_az = 1;
        Mat lon_coef, lat_coef, dem, mappedDem, statevec;
        std::string start_time, end_time, master_file;
        
        {
            NodeUtils::Hdf5Locker locker;
            ret = conversion.read_array_from_h5(input_files[0].c_str(), "mapped_lon", mapped_lon);
            ret += conversion.read_array_from_h5(input_files[0].c_str(), "mapped_lat", mapped_lat);
            if (ret != 0)
            {
                conversion.read_str_from_h5(input_files[0].c_str(), "source_1", source_file);
                QString src_file = save_path + "/" + QString(source_file.c_str());
                master_file = src_file.toStdString();
                ret = conversion.read_int_from_h5(input_files[0].c_str(), "multilook_az", &multilook_az);
                ret = conversion.read_int_from_h5(input_files[0].c_str(), "multilook_rg", &multilook_rg);
                ret = conversion.read_int_from_h5(master_file.c_str(), "range_len", &sceneWidth);
                ret = conversion.read_int_from_h5(master_file.c_str(), "azimuth_len", &sceneHeight);
                ret = conversion.read_int_from_h5(master_file.c_str(), "offset_row", &offset_row);
                ret = conversion.read_int_from_h5(master_file.c_str(), "offset_col", &offset_col);
                ret = conversion.read_array_from_h5(master_file.c_str(), "lon_coefficient", lon_coef);
                ret = conversion.read_array_from_h5(master_file.c_str(), "lat_coefficient", lat_coef);
                ret = conversion.read_double_from_h5(master_file.c_str(), "prf", &prf);
                ret = conversion.read_double_from_h5(master_file.c_str(), "carrier_frequency", &wavelength);
                ret = conversion.read_double_from_h5(master_file.c_str(), "range_spacing", &rangeSpacing);
                ret = conversion.read_double_from_h5(master_file.c_str(), "slant_range_first_pixel", &nearRangeTime);
                ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_start_time", start_time);
                ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_stop_time", end_time);
                ret = conversion.read_array_from_h5(master_file.c_str(), "state_vec", statevec);
            }
        }
        if (ret != 0)
        {
            Deflat flat;
            QString src_file = save_path + "/" + QString(source_file.c_str());
            master_file = src_file.toStdString();
            wavelength = VEL_C / wavelength;
            nearRangeTime = 2.0 * nearRangeTime / VEL_C;
            ret = conversion.utc2gps(start_time.c_str(), &start);
            ret = conversion.utc2gps(end_time.c_str(), &end);
            ret = Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
                &lonMax, &latMax, &lonMin, &latMin);
            ret = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem, &lon_upperleft, &lat_upperleft, lonMin, lonMax, latMin, latMax);
            ret = flat.demMapping(dem, mappedDem, mapped_lat, mapped_lon, lon_upperleft, lat_upperleft, offset_row, offset_col, sceneHeight, sceneWidth,
                prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec, 20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
            //多视操作
            if (multilook_rg > 1 || multilook_az > 1)
            {
                int rows_mapped = sceneHeight / multilook_az;
                int cols_mapped = sceneWidth / multilook_rg;
                Mat lon_new(rows_mapped, cols_mapped, CV_32F);
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
                            cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                    }
                }
                lon_new.copyTo(mapped_lon);
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
                            cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                    }
                }
                lon_new.copyTo(mapped_lat);
            }
        }
        emit updateProcess(20, QStringLiteral("正在地理编码……"));
        double lat_north, lat_south, lon_west, lon_east;
        for (int i = 0; i < input_files.size(); i++)
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                emit errorProcess(QStringLiteral("任务已被中止。"));
                return;
            }

            {
                NodeUtils::Hdf5Locker locker;
                if (product_level == QString("phase-1.0"))
                {
                    ret = conversion.read_array_from_h5(input_files[i].c_str(), "phase", phase);
                    geocode_Rank_level = "phase-1.1";
                }
                if (product_level == QString("phase-2.0"))
                {
                    ret = conversion.read_array_from_h5(input_files[i].c_str(), "phase", phase);
                    geocode_Rank_level = "phase-2.1";
                }
                if (product_level == QString("phase-3.0"))
                {
                    ret = conversion.read_array_from_h5(input_files[i].c_str(), "phase", phase);
                    geocode_Rank_level = "phase-3.1";
                }
                if (product_level == QString("coherence-1.0"))
                {
                    ret = conversion.read_array_from_h5(input_files[i].c_str(), "coherence", phase);
                    geocode_Rank_level = "coherence-1.1";
                }
                if (product_level == QString("dem-1.0"))
                {
                    ret = conversion.read_array_from_h5(input_files[i].c_str(), "dem", phase);
                    geocode_Rank_level = "dem-1.1";
                }
                if (product_level == QString("SBAS-1.0"))
                {
                    ret = conversion.read_array_from_h5(input_files[i].c_str(), "defomation_velocity", phase);
                    geocode_Rank_level = "SBAS-1.1";
                }
            }
            ret = util.SAR2UTM(mapped_lon, mapped_lat, phase, mapped_phase, 1, &lon_east, &lon_west, &lat_north, &lat_south);
            {
                NodeUtils::Hdf5Locker locker;
                ret = conversion.creat_new_h5(output_files[i].c_str());
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_east", lon_east);
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_west", lon_west);
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_north", lat_north);
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_south", lat_south);
                if (product_level == QString("phase-1.0") ||
                    product_level == QString("phase-2.0") ||
                    product_level == QString("phase-3.0")
                    )
                {
                    ret = conversion.write_array_to_h5(output_files[i].c_str(), "phase", mapped_phase);
                }
                if (product_level == QString("coherence-1.0"))
                {
                    ret = conversion.write_array_to_h5(output_files[i].c_str(), "coherence", mapped_phase);
                }
                if (product_level == QString("dem-1.0"))
                {
                    ret = conversion.write_array_to_h5(output_files[i].c_str(), "dem", mapped_phase);
                }
                if (product_level == QString("SBAS-1.0"))
                {
                    ret = conversion.write_array_to_h5(output_files[i].c_str(), "defomation_velocity", mapped_phase);
                }
            }
            int process = 20 + double(i + 1) / (double)input_files.size() * 70.0;

            emit updateProcess(process, QStringLiteral("正在地理编码……"));
        }
    }
    //SAR图像地理编码
    else
    {
        std::string source_file;
        Mat mapped_lat, mapped_lon, amplitude, mapped_amplitude;
        ComplexMat slc;
        geocode_Rank_level = "amplitude-1.1";

        QString project_xmlfile = save_path + "/" + project_name;
        TiXmlElement* pnode = NULL, * pchild = NULL;
        XMLFile xmldoc;
        xmldoc.XMLFile_load(project_xmlfile.toStdString().c_str());
        xmldoc.find_node("DataNode", pnode);
        while (pnode)
        {
            if (0 == strcmp(pnode->Attribute("name"), srcNode.toStdString().c_str())) break;
            pnode = pnode->NextSiblingElement();
        }
        xmldoc._find_node(pnode, "master_image", pchild);
        int masterIndex = 1;
        if (pchild) ret = sscanf(pchild->GetText(), "%d", &masterIndex);

        double lonMax2 = 0, lonMin2 = 0, latMax2 = 0, latMin2 = 0, lon_upperleft2 = 0, lat_upperleft2 = 0, rangeSpacing2 = 0,
            nearRangeTime2 = 0, wavelength2 = 0, prf2 = 0, start2 = 0, end2 = 0;
        int sceneHeight2 = 0, sceneWidth2 = 0, offset_row2 = 0, offset_col2 = 0;
        Mat lon_coef2, lat_coef2, dem2, mappedDem2, statevec2;
        std::string start_time2, end_time2, master_file2;

        {
            NodeUtils::Hdf5Locker locker;
            ret = conversion.read_array_from_h5(input_files[masterIndex - 1].c_str(), "mapped_lon", mapped_lon);
            ret += conversion.read_array_from_h5(input_files[masterIndex - 1].c_str(), "mapped_lat", mapped_lat);
            if (ret != 0)
            {
                master_file2 = input_files[masterIndex - 1];
                ret = conversion.read_int_from_h5(master_file2.c_str(), "range_len", &sceneWidth2);
                ret = conversion.read_int_from_h5(master_file2.c_str(), "azimuth_len", &sceneHeight2);
                ret = conversion.read_int_from_h5(master_file2.c_str(), "offset_row", &offset_row2);
                ret = conversion.read_int_from_h5(master_file2.c_str(), "offset_col", &offset_col2);
                ret = conversion.read_array_from_h5(master_file2.c_str(), "lon_coefficient", lon_coef2);
                ret = conversion.read_array_from_h5(master_file2.c_str(), "lat_coefficient", lat_coef2);
                ret = conversion.read_double_from_h5(master_file2.c_str(), "prf", &prf2);
                ret = conversion.read_double_from_h5(master_file2.c_str(), "carrier_frequency", &wavelength2);
                ret = conversion.read_double_from_h5(master_file2.c_str(), "range_spacing", &rangeSpacing2);
                ret = conversion.read_double_from_h5(master_file2.c_str(), "slant_range_first_pixel", &nearRangeTime2);
                ret = conversion.read_str_from_h5(master_file2.c_str(), "acquisition_start_time", start_time2);
                ret = conversion.read_str_from_h5(master_file2.c_str(), "acquisition_stop_time", end_time2);
                ret = conversion.read_array_from_h5(master_file2.c_str(), "state_vec", statevec2);
            }
        }
        if (ret != 0)
        {
            Deflat flat;
            wavelength2 = VEL_C / wavelength2;
            nearRangeTime2 = 2.0 * nearRangeTime2 / VEL_C;
            ret = conversion.utc2gps(start_time2.c_str(), &start2);
            ret = conversion.utc2gps(end_time2.c_str(), &end2);
            ret = Utils::computeImageGeoBoundry(lat_coef2, lon_coef2, sceneHeight2, sceneWidth2, offset_row2, offset_col2,
                &lonMax2, &latMax2, &lonMin2, &latMin2);
            ret = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem2, &lon_upperleft2, &lat_upperleft2, lonMin2, lonMax2, latMin2, latMax2);
            ret = flat.demMapping(dem2, mappedDem2, mapped_lat, mapped_lon, lon_upperleft2, lat_upperleft2, offset_row2, offset_col2, sceneHeight2, sceneWidth2,
                prf2, rangeSpacing2, wavelength2, nearRangeTime2, start2, end2, statevec2, 20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
        }

        //多视操作
        if (multi_rg > 1 || multi_az > 1)
        {
            int rows_mapped = mapped_lon.rows / multi_az;
            int cols_mapped = mapped_lon.cols / multi_rg;
            Mat lon_new(rows_mapped, cols_mapped, CV_32F);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multi_az, i * multi_az + multi_az),
                        cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
                }
            }
            lon_new.copyTo(mapped_lon);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multi_az, i * multi_az + multi_az),
                        cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
                }
            }
            lon_new.copyTo(mapped_lat);
        }

        emit updateProcess(20, QStringLiteral("正在地理编码……"));
        double lat_north, lat_south, lon_west, lon_east;
        for (int i = 0; i < input_files.size(); i++)
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                emit errorProcess(QStringLiteral("任务已被中止。"));
                return;
            }

            {
                NodeUtils::Hdf5Locker locker;
                ret = conversion.read_slc_from_h5(input_files[i].c_str(), slc);
            }
            slc.convertTo(slc, CV_64F);
            amplitude = slc.GetMod();
            util.multilook_SAR(amplitude, amplitude, multi_rg, multi_az);
            ret = util.SAR2UTM(mapped_lon, mapped_lat, amplitude, mapped_amplitude, 1, &lon_east, &lon_west, &lat_north, &lat_south);
            {
                NodeUtils::Hdf5Locker locker;
                ret = conversion.creat_new_h5(output_files[i].c_str());
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_east", lon_east);
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_west", lon_west);
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_north", lat_north);
                ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_south", lat_south);
                ret = conversion.write_array_to_h5(output_files[i].c_str(), "amplitude", mapped_amplitude);
            }
            int process = 20 + double(i + 1) / (double)input_files.size() * 70.0;
            emit updateProcess(process, QStringLiteral("正在地理编码……"));
        }
    }
    /*建立地理编码根节点*/
    if (model) {
        QMetaObject::invokeMethod(model, [=]() {
            QStandardItem* geocode = NULL;
            for (int i = 0; i < project->rowCount(); i++)
            {
                if (project->child(i, 0)->text() == dstNode)
                {
                    geocode = project->child(i, 0);
                    break;
                }
            }

            if (!geocode)
            {
                geocode = new QStandardItem(dstNode);
                geocode->setToolTip(project_name);
                geocode->setIcon(QIcon(FOLDER_ICON));
                project->appendRow(geocode);
                QStandardItem* geocode_Rank = new QStandardItem(geocode_Rank_level);
                project->setChild(project->rowCount() - 1, 1, geocode_Rank);
            }

            XMLFile xml;
            QString xml_path = save_path + "/" + project_name;
            xml.XMLFile_load(xml_path.toStdString().c_str());
            for (int i = 0; i < input_files.size(); i++)
            {
                QFileInfo fileinfo = QFileInfo(QString(output_files.at(i).c_str()));
                QString geocode_name = fileinfo.baseName();
                QStandardItem* item_img = NULL;
                for (int j = 0; j < geocode->rowCount(); j++)
                {
                    if (geocode->child(j, 0)->text() == geocode_name)
                    {
                        item_img = geocode->child(j, 0);
                        break;
                    }
                }

                if (!item_img)
                {
                    QStandardItem* geocode_images_name = new QStandardItem(geocode_name);
                    if (product_level == QString("coherence-1.0")) geocode_images_name->setToolTip("coherence");
                    else if (product_level == QString("phase-1.0") ||
                        product_level == QString("phase-2.0") ||
                        product_level == QString("phase-3.0")
                        )
                    {
                        geocode_images_name->setToolTip("phase");
                    }
                    else if (product_level == QString("dem-1.0")) geocode_images_name->setToolTip("dem");
                    else if (product_level == QString("SBAS-1.0")) geocode_images_name->setToolTip("SBAS");
                    else geocode_images_name->setToolTip("amplitude");
                    QStandardItem* geocode_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                    geocode_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                    geocode->appendRow(geocode_images_name);
                    geocode->setChild(geocode->rowCount() - 1, 1, geocode_images_path);

                    xml.XMLFile_add_geocoding(dstNode.toStdString().c_str(), geocode_name.toStdString().c_str(),
                        ("/" + dstNode + "/" + geocode_name + ".h5").toStdString().c_str(), geocode_Rank_level.toStdString().c_str());
                }
                else
                {
                    geocode->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                }
            }
            xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
        }, Qt::BlockingQueuedConnection);
    }

    emit sendModel(model);
    emit updateProcess(100, QStringLiteral("完成……"));
    InSARLogManager::LogInfo("GeocodingWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}
