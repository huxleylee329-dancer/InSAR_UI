// GCPImportExport.cpp
#include "include/GCPImportExport.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDebug>
#include <QFileInfo>
#include <cmath>
#include <limits>

// 引入 GDAL/OGR 头文件
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>

bool GCPImportExport::importCSV(const QString& filePath, std::vector<GCPPoint>& gcps)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "GCPImportExport Error: Failed to open CSV file for reading:" << filePath;
        return false;
    }

    QTextStream in(&file);
    if (in.atEnd()) return true;

    // 读取表头并确定列索引
    QString headerLine = in.readLine();
    QStringList headers = headerLine.split(',');
    
    int idxLon = -1, idxLat = -1, idxHgt = -1;
    int idxRow = -1, idxCol = -1, idxDesc = -1, idxQual = -1, idxSrc = -1;

    for (int i = 0; i < headers.size(); ++i) {
        QString header = headers[i].trimmed().toLower();
        if (header == "lon" || header == "longitude" || header == "x" || header == "经度") idxLon = i;
        else if (header == "lat" || header == "latitude" || header == "y" || header == "纬度") idxLat = i;
        else if (header == "height" || header == "hgt" || header == "elevation" || header == "z" || header == "高程") idxHgt = i;
        else if (header == "row" || header == "line" || header == "行号") idxRow = i;
        else if (header == "col" || header == "column" || header == "sample" || header == "列号") idxCol = i;
        else if (header == "description" || header == "desc" || header == "info" || header == "描述") idxDesc = i;
        else if (header == "quality" || header == "qual" || header == "质量") idxQual = i;
        else if (header == "source" || header == "src" || header == "来源") idxSrc = i;
    }

    // 基础校验：至少要有经度和纬度
    if (idxLon == -1 || idxLat == -1) {
        qDebug() << "GCPImportExport Error: CSV file lacks 'lon' or 'lat' columns.";
        return false;
    }

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = line.split(',');
        GCPPoint gcp;

        // 经度和纬度是必须的
        if (fields.size() <= idxLon || fields.size() <= idxLat) continue;
        
        bool okLon = false, okLat = false;
        gcp.lon = fields[idxLon].toDouble(&okLon);
        gcp.lat = fields[idxLat].toDouble(&okLat);

        if (!okLon || !okLat) continue;

        // 高程可选，默认为 0.0
        if (idxHgt != -1 && fields.size() > idxHgt) {
            gcp.height = fields[idxHgt].toDouble();
        }

        // 影像坐标 (Row, Col) 可选，默认为 NaN
        bool hasRow = false, hasCol = false;
        if (idxRow != -1 && fields.size() > idxRow) {
            QString val = fields[idxRow].trimmed();
            if (!val.isEmpty()) {
                gcp.row = val.toDouble(&hasRow);
            }
        }
        if (idxCol != -1 && fields.size() > idxCol) {
            QString val = fields[idxCol].trimmed();
            if (!val.isEmpty()) {
                gcp.col = val.toDouble(&hasCol);
            }
        }
        
        if (!hasRow || !hasCol) {
            gcp.row = std::numeric_limits<double>::quiet_NaN();
            gcp.col = std::numeric_limits<double>::quiet_NaN();
        }

        // 其他字段
        if (idxDesc != -1 && fields.size() > idxDesc) {
            gcp.description = fields[idxDesc].trimmed().toStdString();
        }
        if (idxSrc != -1 && fields.size() > idxSrc) {
            gcp.source = fields[idxSrc].trimmed().toStdString();
        } else {
            gcp.source = "import";
        }
        if (idxQual != -1 && fields.size() > idxQual) {
            gcp.quality = fields[idxQual].toInt();
        }

        gcps.push_back(gcp);
    }

    file.close();
    return true;
}

bool GCPImportExport::exportCSV(const QString& filePath, const std::vector<GCPPoint>& gcps)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "GCPImportExport Error: Failed to open CSV file for writing:" << filePath;
        return false;
    }

    QTextStream out(&file);
    // 写入表头
    out << "lon,lat,height,row,col,description,source,quality\n";

    for (const auto& gcp : gcps) {
        out << QString::number(gcp.lon, 'f', 8) << ","
            << QString::number(gcp.lat, 'f', 8) << ","
            << QString::number(gcp.height, 'f', 3) << ",";
        
        if (gcp.isAnnotated()) {
            out << QString::number(gcp.row, 'f', 3) << ","
                << QString::number(gcp.col, 'f', 3) << ",";
        } else {
            out << ",,"; // 未标注则留空
        }

        out << QString::fromStdString(gcp.description).replace(',', ' ') << ","
            << QString::fromStdString(gcp.source) << ","
            << gcp.quality << "\n";
    }

    file.close();
    return true;
}

bool GCPImportExport::importShapefile(const QString& filePath, std::vector<GCPPoint>& gcps)
{
    // 初始化 OGR 驱动并打开 Shapefile
    GDALAllRegister();
    
    // Windows 环境下将文件名转化为本地编码 (防止中文路径乱码)
    QByteArray localPath = QFile::encodeName(filePath);
    GDALDataset *poDS = (GDALDataset*)GDALOpenEx(localPath.constData(), GDAL_OF_VECTOR, NULL, NULL, NULL);
    
    if (poDS == NULL) {
        qDebug() << "GCPImportExport Error: Failed to open Shapefile:" << filePath;
        return false;
    }

    OGRLayer *poLayer = poDS->GetLayer(0);
    if (poLayer == NULL) {
        GDALClose(poDS);
        return false;
    }

    poLayer->ResetReading();
    OGRFeature *poFeature;

    // 获取字段索引
    OGRFeatureDefn *poFDefn = poLayer->GetLayerDefn();
    int idxRow = poFDefn->GetFieldIndex("row");
    int idxCol = poFDefn->GetFieldIndex("col");
    int idxDesc = poFDefn->GetFieldIndex("desc");
    if (idxDesc == -1) idxDesc = poFDefn->GetFieldIndex("description");
    int idxQual = poFDefn->GetFieldIndex("quality");
    if (idxQual == -1) idxQual = poFDefn->GetFieldIndex("qual");
    int idxSrc = poFDefn->GetFieldIndex("source");
    if (idxSrc == -1) idxSrc = poFDefn->GetFieldIndex("src");

    while ((poFeature = poLayer->GetNextFeature()) != NULL) {
        OGRGeometry *poGeometry = poFeature->GetGeometryRef();
        if (poGeometry != NULL && wkbFlatten(poGeometry->getGeometryType()) == wkbPoint) {
            OGRPoint *poPoint = (OGRPoint*)poGeometry;
            GCPPoint gcp;
            gcp.lon = poPoint->getX();
            gcp.lat = poPoint->getY();
            gcp.height = poPoint->getZ(); // 若无 Z 则默认返回 0.0

            // 读取属性字段
            bool hasRow = false, hasCol = false;
            if (idxRow != -1 && poFeature->IsFieldSetAndNotNull(idxRow)) {
                gcp.row = poFeature->GetFieldAsDouble(idxRow);
                hasRow = true;
            }
            if (idxCol != -1 && poFeature->IsFieldSetAndNotNull(idxCol)) {
                gcp.col = poFeature->GetFieldAsDouble(idxCol);
                hasCol = true;
            }

            if (!hasRow || !hasCol) {
                gcp.row = std::numeric_limits<double>::quiet_NaN();
                gcp.col = std::numeric_limits<double>::quiet_NaN();
            }

            if (idxDesc != -1 && poFeature->IsFieldSetAndNotNull(idxDesc)) {
                gcp.description = poFeature->GetFieldAsString(idxDesc);
            }
            if (idxQual != -1 && poFeature->IsFieldSetAndNotNull(idxQual)) {
                gcp.quality = poFeature->GetFieldAsInteger(idxQual);
            }
            if (idxSrc != -1 && poFeature->IsFieldSetAndNotNull(idxSrc)) {
                gcp.source = poFeature->GetFieldAsString(idxSrc);
            } else {
                gcp.source = "import";
            }

            gcps.push_back(gcp);
        }
        OGRFeature::DestroyFeature(poFeature);
    }

    GDALClose(poDS);
    return true;
}

bool GCPImportExport::exportShapefile(const QString& filePath, const std::vector<GCPPoint>& gcps)
{
    GDALAllRegister();

    // 加载 Shapefile 驱动
    GDALDriver *poDriver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    if (poDriver == NULL) {
        qDebug() << "GCPImportExport Error: ESRI Shapefile driver not available.";
        return false;
    }

    // Windows 中文路径转本地编码
    QByteArray localPath = QFile::encodeName(filePath);
    
    // 如果文件已存在，先删除它
    poDriver->Delete(localPath.constData());

    // 创建新的 Dataset
    GDALDataset *poDS = poDriver->Create(localPath.constData(), 0, 0, 0, GDT_Unknown, NULL);
    if (poDS == NULL) {
        qDebug() << "GCPImportExport Error: Failed to create Shapefile:" << filePath;
        return false;
    }

    // 定义 WGS84 坐标系 (EPSG:4326)
    OGRSpatialReference oSRS;
    oSRS.importFromEPSG(4326);

    // 创建点图层
    OGRLayer *poLayer = poDS->CreateLayer("gcp", &oSRS, wkbPoint25D, NULL);
    if (poLayer == NULL) {
        qDebug() << "GCPImportExport Error: Failed to create layer.";
        GDALClose(poDS);
        return false;
    }

    // 定义属性表字段
    OGRFieldDefn oFieldRow("row", OFTReal);
    OGRFieldDefn oFieldCol("col", OFTReal);
    OGRFieldDefn oFieldDesc("desc", OFTString);
    oFieldDesc.SetWidth(80);
    OGRFieldDefn oFieldSrc("source", OFTString);
    oFieldSrc.SetWidth(20);
    OGRFieldDefn oFieldQual("quality", OFTInteger);

    poLayer->CreateField(&oFieldRow);
    poLayer->CreateField(&oFieldCol);
    poLayer->CreateField(&oFieldDesc);
    poLayer->CreateField(&oFieldSrc);
    poLayer->CreateField(&oFieldQual);

    // 写入要素
    for (const auto& gcp : gcps) {
        OGRFeature *poFeature = OGRFeature::CreateFeature(poLayer->GetLayerDefn());
        
        // 创建点几何项
        OGRPoint point;
        point.setX(gcp.lon);
        point.setY(gcp.lat);
        point.setZ(gcp.height);
        poFeature->SetGeometry(&point);

        // 设置属性
        if (gcp.isAnnotated()) {
            poFeature->SetField("row", gcp.row);
            poFeature->SetField("col", gcp.col);
        } else {
            // 未标注字段留空 (NULL)
            poFeature->SetFieldNull(poFeature->GetFieldIndex("row"));
            poFeature->SetFieldNull(poFeature->GetFieldIndex("col"));
        }

        poFeature->SetField("desc", QString::fromStdString(gcp.description).toLocal8Bit().constData());
        poFeature->SetField("source", gcp.source.c_str());
        poFeature->SetField("quality", gcp.quality);

        if (poLayer->CreateFeature(poFeature) != OGRERR_NONE) {
            qDebug() << "GCPImportExport Error: Failed to create feature in shapefile.";
            OGRFeature::DestroyFeature(poFeature);
            GDALClose(poDS);
            return false;
        }

        OGRFeature::DestroyFeature(poFeature);
    }

    GDALClose(poDS);
    return true;
}
