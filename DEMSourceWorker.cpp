#include "DEMSourceWorker.h"
#include <demsourcemanager.h>
#include <FormatConversion.h>
#include <Utils.h>
#include <InSARLogManager.h>
#include <icon_source.h>
#include <QThread>
#include "NodeUtils.h"
#include <gdal_priv.h>

static bool write_dem_to_tif(const QString& tifPath, const cv::Mat& dem, const double* gt, const char* wkt)
{
    GDALAllRegister();
    GDALDriver* poDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!poDriver) return false;

    int cols = dem.cols;
    int rows = dem.rows;
    GDALDataset* poDstDS = poDriver->Create(tifPath.toLocal8Bit().constData(), cols, rows, 1, GDT_Float32, nullptr);
    if (!poDstDS) return false;

    poDstDS->SetGeoTransform(const_cast<double*>(gt));
    poDstDS->SetProjection(wkt);

    GDALRasterBand* poBand = poDstDS->GetRasterBand(1);
    poBand->SetNoDataValue(-32767.0);
    
    cv::Mat floatDem;
    if (dem.type() != CV_32F) {
        dem.convertTo(floatDem, CV_32F);
    } else {
        floatDem = dem;
    }

    CPLErr err = poBand->RasterIO(GF_Write, 0, 0, cols, rows, floatDem.data, cols, rows, GDT_Float32, 0, 0);
    GDALClose(poDstDS);

    return err == CE_None;
}

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QEventLoop>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QtMath>
#include <QTextStream>

#ifdef _DEBUG
#pragma comment(lib, "Dem_d.lib")
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Dem.lib")
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif



using namespace std;
using namespace cv;

DEMSourceWorker::DEMSourceWorker(QObject* parent)
    : BaseWorker(parent)
{
}

DEMSourceWorker::~DEMSourceWorker()
{
}

bool DEMSourceWorker::downloadTile(const QString& url, const QString& savePath)
{
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    // 从全局 Config.ini 读取 Earthdata 账号密码
    QSettings settings("Config.ini", QSettings::IniFormat);
    QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
    QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();

    QString username = QString::fromUtf8(QByteArray::fromBase64(encryptedUser.toUtf8()));
    QString password = QString::fromUtf8(QByteArray::fromBase64(encryptedPass.toUtf8()));

    connect(&manager, &QNetworkAccessManager::authenticationRequired,
            this, [&](QNetworkReply*, QAuthenticator* authenticator) {
                authenticator->setUser(username);
                authenticator->setPassword(password);
            });

    QString tempPath = savePath + ".part";
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly))
    {
        InSARLogManager::LogError("DEMSourceWorker", QString("Failed to open temp file for write: ") + tempPath);
        return false;
    }

    QNetworkReply* reply = manager.get(request);

    // 绑定读取信号
    connect(reply, &QNetworkReply::readyRead, this, [&]() {
        tempFile.write(reply->readAll());
    });

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(); // 阻塞当前线程直到下载完毕

    tempFile.close();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && (statusCode == 200 || statusCode == 206))
    {
        if (QFile::exists(savePath))
        {
            QFile::remove(savePath);
        }
        tempFile.rename(savePath);
        reply->deleteLater();
        return true;
    }
    else
    {
        InSARLogManager::LogError("DEMSourceWorker", 
            QString("Download failed: URL: %1, Status Code: %2, Error: %3")
            .arg(url).arg(statusCode).arg(reply->errorString()));
        tempFile.remove();
        reply->deleteLater();
        return false;
    }
}

void DEMSourceWorker::fetch_dem(
    QString projectPath,
    QString projectName,
    QString dstNode,
    QStringList filePaths,
    int demSource,
    double targetResolution,
    QString cacheDir,
    QStandardItemModel* model
)
{
    InSARLogManager::LogInfo("DEMSourceWorker", QString("External DEM fetch started. Target node: %1, Source Type: %2").arg(dstNode).arg(demSource));

    if (projectPath.isEmpty() || projectName.isEmpty() || dstNode.isEmpty() || filePaths.isEmpty() || !model)
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    emit updateProcess(5, QStringLiteral("准备解析范围……"));

    // 1. 获取绝对工作目录（防止二次剥离目录路径）
    QString save_path = projectPath;
    if (projectPath.endsWith(".insar", Qt::CaseInsensitive))
    {
        save_path = QFileInfo(projectPath).absolutePath();
    }

    // 2. 精准提取 AOI 范围
    double min_lon = 0, max_lon = 0, min_lat = 0, max_lat = 0;
    bool aoi_ok = false;
    QString firstInput = filePaths.first();
    if (QFileInfo(firstInput).isRelative())
    {
        firstInput = save_path + "/" + firstInput;
    }

    FormatConversion FC;

    // 检查是否存在 mapped_lon/mapped_lat (已地理编码的 H5)并从其提取 AOI 范围
    Mat mat_lon, mat_lat;
    bool mapped_check = false;
    {
        NodeUtils::Hdf5Locker locker;
        mapped_check = (NodeUtils::readMatFromH5(firstInput, "mapped_lon", mat_lon) &&
                        NodeUtils::readMatFromH5(firstInput, "mapped_lat", mat_lat));
    }
    if (mapped_check)
    {
        double min_lon_val, max_lon_val, min_lat_val, max_lat_val;
        minMaxLoc(mat_lon, &min_lon_val, &max_lon_val);
        minMaxLoc(mat_lat, &min_lat_val, &max_lat_val);
        min_lon = min_lon_val;
        max_lon = max_lon_val;
        min_lat = min_lat_val;
        max_lat = max_lat_val;
        aoi_ok = true;
        InSARLogManager::LogInfo("DEMSourceWorker", QString("Extracted AOI from mapped coordinates: Lon[%1, %2], Lat[%3, %4]").arg(min_lon).arg(max_lon).arg(min_lat).arg(max_lat));
    }

    if (!aoi_ok)
    {
        // 尝试从雷达多项式系数计算边界
        std::string source_file;
        QString src_file;
        bool read_src_ok = false;
        {
            NodeUtils::Hdf5Locker locker;
            read_src_ok = NodeUtils::readStringFromH5(firstInput, "source_1", source_file);
        }
        if (read_src_ok)
        {
            src_file = save_path + "/" + QString(source_file.c_str());
        }
        else
        {
            // 否则本身即是原始 SLC 或裁剪影像，直接以输入 H5 文件自身作为参数源文件
            src_file = firstInput;
        }

        if (QFile::exists(src_file))
        {
            int sceneHeight = 0, sceneWidth = 0, offset_row = 0, offset_col = 0;
            Mat lon_coef, lat_coef;
            bool read_para_ok = false;
            {
                NodeUtils::Hdf5Locker locker;
                read_para_ok = (NodeUtils::readScalarFromH5(src_file, "range_len", sceneWidth) &&
                                NodeUtils::readScalarFromH5(src_file, "azimuth_len", sceneHeight) &&
                                NodeUtils::readScalarFromH5(src_file, "offset_row", offset_row) &&
                                NodeUtils::readScalarFromH5(src_file, "offset_col", offset_col) &&
                                NodeUtils::readMatFromH5(src_file, "lon_coefficient", lon_coef) &&
                                NodeUtils::readMatFromH5(src_file, "lat_coefficient", lat_coef));
            }
            if (read_para_ok)
            {
                double lonMax = 0, lonMin = 0, latMax = 0, latMin = 0;
                if (Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
                    &lonMax, &latMax, &lonMin, &latMin) == 0)
                {
                    min_lon = lonMin;
                    max_lon = lonMax;
                    min_lat = latMin;
                    max_lat = latMax;
                    aoi_ok = true;
                    InSARLogManager::LogInfo("DEMSourceWorker", QString("Extracted AOI from radar geometry boundary: Lon[%1, %2], Lat[%3, %4]").arg(min_lon).arg(max_lon).arg(min_lat).arg(max_lat));
                }
            }
        }
    }

    if (!aoi_ok)
    {
        emit errorProcess(QStringLiteral("解析输入数据地理包围框失败，请检查上游节点数据是否正确。"));
        return;
    }

    // 外扩 0.05 度以保证边缘插值时不溢出
    min_lon -= 0.05;
    max_lon += 0.05;
    min_lat -= 0.05;
    max_lat += 0.05;

    // 3. 缓存目录及多源隔离初始化
    if (cacheDir.isEmpty())
    {
        cacheDir = QCoreApplication::applicationDirPath() + "/dem";
    }
    
    QString subDirName = "srtm1";
    if (demSource == 1) subDirName = "srtm3";
    else if (demSource == 2) subDirName = "copernicus";
    else if (demSource == 3) subDirName = "aster";

    QString fullCachePath = cacheDir + "/" + subDirName;
    QDir().mkpath(fullCachePath);

    // 4. 计算瓦片跨度并准备下载/检索
    int startLon = qFloor(min_lon);
    int endLon = qFloor(max_lon);
    int startLat = qFloor(min_lat);
    int endLat = qFloor(max_lat);

    QStringList cachedFiles;
    emit updateProcess(10, QStringLiteral("检索本地缓存及下载瓦片中……"));

    for (int lat = startLat; lat <= endLat; ++lat)
    {
        for (int lon = startLon; lon <= endLon; ++lon)
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                emit errorProcess(QStringLiteral("任务已被中断。"));
                return;
            }

            char latNS = (lat >= 0) ? 'N' : 'S';
            char lonEW = (lon >= 0) ? 'E' : 'W';
            QString tileName = QString("%1%2%3%4")
                .arg(latNS)
                .arg(qAbs(lat), 2, 10, QChar('0'))
                .arg(lonEW)
                .arg(qAbs(lon), 3, 10, QChar('0'));

            QString expectedFile = fullCachePath + "/" + tileName;
            if (demSource == 0 || demSource == 1)
            {
                expectedFile += ".hgt";
            }
            else
            {
                expectedFile += ".tif";
            }

            // 检查缓存
            bool tileFound = false;
            if (QFile::exists(expectedFile))
            {
                tileFound = true;
            }
            else
            {
                // 如果是 zip，尝试解压
                QString zipFile = fullCachePath + "/" + tileName;
                if (demSource == 0) zipFile += ".SRTMGL1.hgt.zip";
                else if (demSource == 1) zipFile += ".SRTMGL3.hgt.zip";
                else zipFile += ".zip";

                if (QFile::exists(zipFile))
                {
                    // 调用已有的 DigitalElevationModel::unzip
                    if (DigitalElevationModel::unzip(zipFile.toLocal8Bit().constData(), fullCachePath.toLocal8Bit().constData()) == 0)
                    {
                        if (QFile::exists(expectedFile))
                        {
                            tileFound = true;
                        }
                    }
                }
            }

            // 如果本地无缓存，执行下载
            if (!tileFound)
            {
                QString downloadUrl;
                QString targetZipOrTif = expectedFile;

                if (demSource == 0) // SRTM 1"
                {
                    downloadUrl = QString("https://e4ftl01.cr.usgs.gov/MEASURES/SRTMGL1.003/2000.02.11/%1.SRTMGL1.hgt.zip").arg(tileName);
                    targetZipOrTif = fullCachePath + "/" + tileName + ".SRTMGL1.hgt.zip";
                }
                else if (demSource == 1) // SRTM 3"
                {
                    downloadUrl = QString("https://e4ftl01.cr.usgs.gov/MEASURES/SRTMGL3.003/2000.02.11/%1.SRTMGL3.hgt.zip").arg(tileName);
                    targetZipOrTif = fullCachePath + "/" + tileName + ".SRTMGL3.hgt.zip";
                }
                else if (demSource == 2) // Copernicus 30m
                {
                    // 使用 AWS S3 公共免密源
                    char ns = (lat >= 0) ? 'N' : 'S';
                    char ew = (lon >= 0) ? 'E' : 'W';
                    QString tileAWS = QString("Copernicus_DSM_COG_10_%1%2_00_%3%4_00_DEM")
                        .arg(ns)
                        .arg(qAbs(lat), 2, 10, QChar('0'))
                        .arg(ew)
                        .arg(qAbs(lon), 3, 10, QChar('0'));
                    downloadUrl = QString("https://copernicus-dem-30m.s3.amazonaws.com/%1/%2.tif").arg(tileAWS).arg(tileAWS);
                }
                else if (demSource == 3) // ASTER GDEM
                {
                    downloadUrl = QString("https://e4ftl01.cr.usgs.gov/ASTT/ASTGTM.003/2000.03.01/ASTGTMV003_%1_dem.zip").arg(tileName);
                    targetZipOrTif = fullCachePath + "/" + tileName + "_dem.zip";
                }

                emit updateProcess(10 + (lat - startLat) * 30 / (endLat - startLat + 1), QStringLiteral("正在下载 DEM 瓦片 %1……").arg(tileName));
                
                if (downloadTile(downloadUrl, targetZipOrTif))
                {
                    // 如果是 zip，解压它
                    if (targetZipOrTif.endsWith(".zip"))
                    {
                        DigitalElevationModel::unzip(targetZipOrTif.toLocal8Bit().constData(), fullCachePath.toLocal8Bit().constData());
                        // 下载完后清理临时压缩包
                        QFile::remove(targetZipOrTif);
                    }
                    
                    if (QFile::exists(expectedFile))
                    {
                        tileFound = true;
                    }
                }
            }

            if (tileFound)
            {
                cachedFiles.append(expectedFile);
            }
            else
            {
                emit errorProcess(QStringLiteral("获取 DEM 瓦片 %1 失败，请检查网络设置或手动下载至目录：%2").arg(tileName).arg(fullCachePath));
                return;
            }
        }
    }

    if (cachedFiles.isEmpty())
    {
        emit errorProcess(QStringLiteral("未找到任何有效的本地缓存或下载瓦片。"));
        return;
    }

    emit updateProcess(50, QStringLiteral("构建瓦片拼接与重采样……"));

    // 5. 多瓦片拼接机制 (VRT)
    QString finalInputFile;
    QString vrtPath = save_path + "/" + dstNode + "/mosaic.vrt";
    QDir().mkpath(save_path + "/" + dstNode);

    if (cachedFiles.size() == 1)
    {
        finalInputFile = cachedFiles.first();
    }
    else
    {
        // 拼接成 VRT
        QFile vrtFile(vrtPath);
        if (!vrtFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            emit errorProcess(QStringLiteral("创建拼接虚拟描述文件(vrt)失败。"));
            return;
        }

        QTextStream out(&vrtFile);
        out.setCodec("UTF-8");

        // 统一按数据源类型计算瓦片大小及重叠步长，防大小不一致导致 GDAL 无法打开 VRT
        int tileW = 3601;
        int tileH = 3601;
        int stepW = 3600;
        int stepH = 3600;
        bool hasOverlap = true;

        if (demSource == 1) // SRTM 3"
        {
            tileW = 1201;
            tileH = 1201;
            stepW = 1200;
            stepH = 1200;
            hasOverlap = true;
        }
        else if (demSource == 2 || demSource == 3) // Copernicus 30m 或 ASTER GDEM
        {
            tileW = 3600;
            tileH = 3600;
            stepW = 3600;
            stepH = 3600;
            hasOverlap = false;
        }

        double res = (demSource == 1) ? (3.0 / 3600.0) : (1.0 / 3600.0);
        double vrtMinLon = startLon;
        double vrtMaxLat = endLat + 1.0;
        int totalW = (endLon - startLon + 1) * stepW + (hasOverlap ? 1 : 0);
        int totalH = (endLat - startLat + 1) * stepH + (hasOverlap ? 1 : 0);

        out << "<VRTDataset rasterXSize=\"" << totalW << "\" rasterYSize=\"" << totalH << "\">\n";
        out << "  <SRS>GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]</SRS>\n";
        out << "  <GeoTransform>" << vrtMinLon << ", " << res << ", 0.0, " << vrtMaxLat << ", 0.0, " << -res << "</GeoTransform>\n";
        out << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        out << "    <NoDataValue>-32767</NoDataValue>\n";

        for (const QString& file : cachedFiles)
        {
            QFileInfo fi(file);
            QString tName = fi.baseName(); // NxxExxx
            
            // 提取经纬度
            char ns = tName.at(0).toLatin1();
            int latVal = tName.mid(1, 2).toInt();
            if (ns == 'S' || ns == 's') latVal = -latVal;

            char ew = tName.at(3).toLatin1();
            int lonVal = tName.mid(4, 3).toInt();
            if (ew == 'W' || ew == 'w') lonVal = -lonVal;

            int xOff = qRound((lonVal - vrtMinLon) * stepW);
            int yOff = qRound((vrtMaxLat - (latVal + 1.0)) * stepH);

            out << "    <SimpleSource>\n";
            out << "      <SourceFilename relativeToVRT=\"0\">" << QDir::cleanPath(file) << "</SourceFilename>\n";
            out << "      <SourceBand>1</SourceBand>\n";
            out << "      <SourceProperties RasterXSize=\"" << tileW << "\" RasterYSize=\"" << tileH << "\" DataType=\"Float32\" />\n";
            out << "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" << tileW << "\" ySize=\"" << tileH << "\" />\n";
            out << "      <DstRect xOff=\"" << xOff << "\" yOff=\"" << yOff << "\" xSize=\"" << tileW << "\" ySize=\"" << tileH << "\" />\n";
            out << "    </SimpleSource>\n";
        }

        out << "  </VRTRasterBand>\n";
        out << "</VRTDataset>\n";
        vrtFile.close();

        finalInputFile = vrtPath;
    }

    emit updateProcess(65, QStringLiteral("裁剪高程数据中……"));

    // 6. 调用 DEMSourceManager 执行裁剪转换
    DEMSourceManager manager;
    Mat cropped_dem, dem_x, dem_y, dem_z;
    double new_gt[6] = { 0 };
    char wkt_projection[1024] = { 0 };



    int ret = manager.read_crop_and_resample_dem(
        QDir::toNativeSeparators(finalInputFile).toLocal8Bit().constData(),
        min_lon, max_lon,
        min_lat, max_lat,
        targetResolution,
        cropped_dem,
        new_gt,
        wkt_projection,
        1024
    );

    if (ret != 0)
    {
        emit errorProcess(QStringLiteral("裁剪 DEM 数据失败：") + QString::fromLocal8Bit(manager.error_msg));
        return;
    }

    emit updateProcess(80, QStringLiteral("转换高程坐标至 ECEF……"));
    ret = manager.convert_dem_to_ecef(
        cropped_dem,
        new_gt,
        wkt_projection,
        dem_x,
        dem_y,
        dem_z
    );

    if (ret != 0)
    {
        emit errorProcess(QStringLiteral("坐标系转换至地心坐标(ECEF)失败。"));
        return;
    }

    emit updateProcess(90, QStringLiteral("写入 H5 数据文件……"));

    // 7. 写入 H5 文件
    QString outputH5Name = dstNode + "_dem.h5";
    QString outputH5Path = save_path + "/" + dstNode + "/" + outputH5Name;

    // 7.5 写入 TIF 成果文件（供下游工作流节点使用）
    QString outputTifName = dstNode + "_dem.tif";
    QString outputTifPath = save_path + "/" + dstNode + "/" + outputTifName;
    write_dem_to_tif(outputTifPath, cropped_dem, new_gt, wkt_projection);

    // 写入经纬度辅助 2D 矩阵
    int rows = cropped_dem.rows;
    int cols = cropped_dem.cols;
    Mat out_lon(rows, cols, CV_64FC1);
    Mat out_lat(rows, cols, CV_64FC1);

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            // 使用严密的仿射半像素中心偏移公式修正
            double col_c = c + 0.5;
            double row_c = r + 0.5;
            out_lon.at<double>(r, c) = new_gt[0] + col_c * new_gt[1] + row_c * new_gt[2];
            out_lat.at<double>(r, c) = new_gt[3] + col_c * new_gt[4] + row_c * new_gt[5];
        }
    }

    bool write_success = false;
    // 确定数据源名称
    string srcName = "SRTM1";
    if (demSource == 1) srcName = "SRTM3";
    else if (demSource == 2) srcName = "Copernicus";
    else if (demSource == 3) srcName = "ASTER";

    {
        NodeUtils::Hdf5Locker locker;
        if (FC.creat_new_h5(outputH5Path.toStdString().c_str()) == 0)
        {
            // 确保高程和三维坐标使用双精度(CV_64F)写入，防止 precompiled DLL 读取时发生类型 Mismatch
            if (cropped_dem.type() != CV_64F) cropped_dem.convertTo(cropped_dem, CV_64F);
            if (dem_x.type() != CV_64F) dem_x.convertTo(dem_x, CV_64F);
            if (dem_y.type() != CV_64F) dem_y.convertTo(dem_y, CV_64F);
            if (dem_z.type() != CV_64F) dem_z.convertTo(dem_z, CV_64F);

            // 写入数据集
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem", cropped_dem);
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem_x", dem_x);
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem_y", dem_y);
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem_z", dem_z);

            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "lon", out_lon);
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "lat", out_lat);

            // 写入元数据属性
            FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_source", srcName.c_str());
            FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_min_lon", min_lon);
            FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_max_lon", max_lon);
            FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_min_lat", min_lat);
            FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_max_lat", max_lat);
            FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_cache_path", QDir::toNativeSeparators(cacheDir).toStdString().c_str());

            // 写入行列偏移量以向下兼容
            Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
            tmp_int.at<int>(0, 0) = 0;
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "offset_row", tmp_int);
            FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "offset_col", tmp_int);
            write_success = true;
        }
    }

    if (!write_success)
    {
        emit errorProcess(QStringLiteral("创建或写入输出 H5 文件失败。"));
        return;
    }

    // 8. 挂载到项目树及更新 XML
    XMLFile xml;
    QString xml_path = save_path + "/" + projectName;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive))
    {
        xml_path += ".Insar";
    }

    if (xml.XMLFile_load(xml_path.toStdString().c_str()) == 0)
    {
        xml.XMLFile_add_dem(dstNode.toStdString().c_str(), 
                            (dstNode + "_dem").toStdString().c_str(),
                            ("/" + dstNode + "/" + outputH5Name).toStdString().c_str(),
                            0, 0, srcName.c_str(), targetResolution);
        xml.XMLFile_save(xml_path.toStdString().c_str());
    }

    // 挂载项目树 UI
    QStandardItem* project = nullptr;
    QList<QStandardItem*> foundProjects = model->findItems(projectName);
    if (!foundProjects.isEmpty())
    {
        project = foundProjects.first();
    }

    if (project)
    {
        QMetaObject::invokeMethod(model, [=]() {
            QStandardItem* demNode = nullptr;
            for (int i = 0; i < project->rowCount(); ++i)
            {
                if (project->child(i, 0)->text() == dstNode)
                {
                    demNode = project->child(i, 0);
                    break;
                }
            }

            if (!demNode)
            {
                demNode = new QStandardItem(dstNode);
                demNode->setToolTip(projectName);
                demNode->setIcon(QIcon(FOLDER_ICON));
                int insertIndex = 0;
                for (; insertIndex < project->rowCount(); ++insertIndex)
                {
                    QString t = project->child(insertIndex, 1)->text();
                    if (t == "complex-0.0" || t == "complex-1.0" || t == "complex-2.0" || 
                        t == "phase-1.0" || t == "phase-2.0" || t == "phase-3.0" || t == "dem-1.0")
                    {
                        continue;
                    }
                    break;
                }
                project->insertRow(insertIndex, demNode);
                project->setChild(insertIndex, 1, new QStandardItem("dem-1.0"));
            }

            QStandardItem* itemImg = nullptr;
            QString imgName = dstNode + "_dem";
            for (int j = 0; j < demNode->rowCount(); ++j)
            {
                if (demNode->child(j, 0)->text() == imgName)
                {
                    itemImg = demNode->child(j, 0);
                    break;
                }
            }

            if (!itemImg)
            {
                QStandardItem* image = new QStandardItem(imgName);
                image->setToolTip("dem");
                image->setIcon(QIcon(IMAGEDATA_ICON));
                demNode->appendRow(image);
                demNode->setChild(demNode->rowCount() - 1, 1, new QStandardItem(outputH5Path));
            }
            else
            {
                demNode->setChild(itemImg->row(), 1, new QStandardItem(outputH5Path));
            }
        }, Qt::BlockingQueuedConnection);
    }

    // 清理临时 .vrt 文件
    if (!vrtPath.isEmpty() && QFile::exists(vrtPath))
    {
        QFile::remove(vrtPath);
    }

    emit updateProcess(100, QStringLiteral("外部 DEM 获取完成。"));
    emit sendModel(model);
    emit endProcess();
}
