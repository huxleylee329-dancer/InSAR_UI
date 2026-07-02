// include/GCPImportExport.h
#pragma once
#include <QString>
#include <vector>
#include "GCPPoint.h"

class GCPImportExport
{
public:
    // CSV 导入导出
    static bool importCSV(const QString& filePath, std::vector<GCPPoint>& gcps);
    static bool exportCSV(const QString& filePath, const std::vector<GCPPoint>& gcps);

    // Shapefile (SHP) 导入导出 (基于 GDAL/OGR)
    static bool importShapefile(const QString& filePath, std::vector<GCPPoint>& gcps);
    static bool exportShapefile(const QString& filePath, const std::vector<GCPPoint>& gcps);
};
