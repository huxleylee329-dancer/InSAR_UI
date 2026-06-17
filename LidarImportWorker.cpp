#include "LidarImportWorker.h"
#include <FormatConversion.h>
#include <opencv2/core.hpp>

LidarImportWorker::LidarImportWorker(QObject* parent)
    : BaseImportWorker("Lidar", parent)
{
}

LidarImportWorker::~LidarImportWorker()
{
}

bool LidarImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                   int progressMin, int progressMax)
{
    if (arguments.size() < 3) return false;

    QString input_file = arguments[0];
    QString product_type = arguments[1];
    int rh_percentile = arguments[2].toInt();

    FormatConversion conversion;
    std::string file_std = input_file.toStdString();
    std::string h5_path_std = outputPath.toStdString();
    int ret = -1;

    if (product_type == "GEDI L2A")
    {
        cv::Mat rh, lon, lat, dem, quality;
        ret = conversion.read_height_metric_from_GEDI_L2A(file_std.c_str(), rh, lon, lat, dem, quality, rh_percentile);
        if (ret >= 0)
        {
            ret = conversion.creat_new_h5(h5_path_std.c_str());
            if (ret >= 0)
            {
                conversion.write_array_to_h5(h5_path_std.c_str(), "rh", rh);
                conversion.write_array_to_h5(h5_path_std.c_str(), "lon", lon);
                conversion.write_array_to_h5(h5_path_std.c_str(), "lat", lat);
                conversion.write_array_to_h5(h5_path_std.c_str(), "dem", dem);
                conversion.write_array_to_h5(h5_path_std.c_str(), "quality", quality);
            }
        }
    }
    else if (product_type == "GEDI L2B")
    {
        cv::Mat rh100, lowestmode, highestreturn, lon, lat, dem, quality;
        ret = conversion.read_height_metric_from_GEDI_L2B(file_std.c_str(), rh100, lowestmode, highestreturn, lon, lat, dem, quality);
        if (ret >= 0)
        {
            ret = conversion.creat_new_h5(h5_path_std.c_str());
            if (ret >= 0)
            {
                conversion.write_array_to_h5(h5_path_std.c_str(), "rh100", rh100);
                conversion.write_array_to_h5(h5_path_std.c_str(), "elev_lowestmode", lowestmode);
                conversion.write_array_to_h5(h5_path_std.c_str(), "elev_highestreturn", highestreturn);
                conversion.write_array_to_h5(h5_path_std.c_str(), "lon", lon);
                conversion.write_array_to_h5(h5_path_std.c_str(), "lat", lat);
                conversion.write_array_to_h5(h5_path_std.c_str(), "dem", dem);
                conversion.write_array_to_h5(h5_path_std.c_str(), "quality", quality);
            }
        }
    }
    else if (product_type == "ICESat-2 L3A")
    {
        cv::Mat rh, lon, lat, dem, quality;
        ret = conversion.read_height_metric_from_ICESat_2_L3A(file_std.c_str(), rh, lon, lat, dem, quality, rh_percentile);
        if (ret >= 0)
        {
            ret = conversion.creat_new_h5(h5_path_std.c_str());
            if (ret >= 0)
            {
                conversion.write_array_to_h5(h5_path_std.c_str(), "rh", rh);
                conversion.write_array_to_h5(h5_path_std.c_str(), "lon", lon);
                conversion.write_array_to_h5(h5_path_std.c_str(), "lat", lat);
                conversion.write_array_to_h5(h5_path_std.c_str(), "dem", dem);
                conversion.write_array_to_h5(h5_path_std.c_str(), "quality", quality);
            }
        }
    }

    return ret >= 0;
}
