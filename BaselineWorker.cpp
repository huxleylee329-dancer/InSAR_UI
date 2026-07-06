#include "BaselineWorker.h"
#include "Utils.h"
#include "FormatConversion.h"
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QThread>
#include <QFileInfo>
#include <vector>
#include <string>

using namespace std;
using namespace cv;

BaselineWorker::BaselineWorker(QObject* parent)
    : BaseWorker(parent)
{
}

BaselineWorker::~BaselineWorker()
{
}

void BaselineWorker::Baseline_Estimate(int index, const QStringList& filePaths)
{
    if (index < 1 || filePaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或没有文件！"));
        return;
    }

    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("BaselineWorker", QString("Starting Baseline Estimate. Master Index: %1, Image Count: %2").arg(index).arg(filePaths.size()));

    Utils util;
    vector<cv::String> SAR_images;
    for (const QString& path : filePaths) {
        SAR_images.push_back(path.toStdString());
    }

    int image_number = SAR_images.size();
    if (index > image_number) {
        emit errorProcess(QStringLiteral("主图像索引超出范围！"));
        return;
    }

    QList<double> spatial_baseline;
    QList<double> temporal_baseline;

    emit updateProcess(10, QStringLiteral("开始进行基线估计……"));
    FormatConversion FC;

    /*获取主星参数*/
    Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
    Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
    double interp_interval;
    int offset_row, offset_col;
    int Rows, Cols;
    double time_Master = 0;
    string time_master_str;

    try {
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "state_vec", State_Vec_Master);
        if (!State_Vec_Master.empty() && State_Vec_Master.type() != CV_64F) State_Vec_Master.convertTo(State_Vec_Master, CV_64F);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lon_coefficient", Lon_Coeff_Master);
        if (!Lon_Coeff_Master.empty() && Lon_Coeff_Master.type() != CV_64F) Lon_Coeff_Master.convertTo(Lon_Coeff_Master, CV_64F);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lat_coefficient", Lat_Coeff_Master);
        if (!Lat_Coeff_Master.empty() && Lat_Coeff_Master.type() != CV_64F) Lat_Coeff_Master.convertTo(Lat_Coeff_Master, CV_64F);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "prf", tmp_double);
        if (!tmp_double.empty() && tmp_double.type() != CV_64F) {
            tmp_double.convertTo(tmp_double, CV_64F);
        }
        interp_interval = 1 / tmp_double.at<double>(0, 0);

        Mat tmp = Mat::zeros(1, 1, CV_32SC1);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "offset_row", tmp);
        offset_row = tmp.at<int>(0, 0);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "offset_col", tmp);
        offset_col = tmp.at<int>(0, 0);

        FC.read_str_from_h5(SAR_images.at(index - 1).c_str(), "acquisition_start_time", time_master_str);
        FC.utc2gps(time_master_str.c_str(), &time_Master);

        ComplexMat SLC;
        FC.read_slc_from_h5(SAR_images.at(index - 1).c_str(), SLC);
        Rows = SLC.GetRows();
        Cols = SLC.GetCols();
    }
    catch (const std::exception& e) {
        emit errorProcess(QStringLiteral("读取主图像 H5 参数失败: ") + QString::fromStdString(e.what()));
        return;
    }
    catch (...) {
        emit errorProcess(QStringLiteral("读取主图像 H5 参数时发生未知错误！"));
        return;
    }

    Mat cc = Mat::zeros(2, image_number, CV_64F);
    for (int i = 0; i < image_number; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested())
        {
            InSARLogManager::LogInfo("BaselineWorker", "Baseline Estimate interrupted by user.");
            return;
        }

        /*估计时空基线*/
        if (i == index - 1)  //主图像
        {
            temporal_baseline.push_back(0);
            spatial_baseline.push_back(0);
        }
        else
        {
            Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
            double interp_interval_slave;
            double V_baseline = 0, H_baseline = 0;
            double sigma_V = 0, sigma_H = 0;
            double time_Slave = 0;
            string time_slave_str;

            try {
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "state_vec", State_Vec_Slave);
                if (!State_Vec_Slave.empty() && State_Vec_Slave.type() != CV_64F) State_Vec_Slave.convertTo(State_Vec_Slave, CV_64F);
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "lon_coefficient", Lon_Coeff_Slave);
                if (!Lon_Coeff_Slave.empty() && Lon_Coeff_Slave.type() != CV_64F) Lon_Coeff_Slave.convertTo(Lon_Coeff_Slave, CV_64F);
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "lat_coefficient", Lat_Coeff_Slave);
                if (!Lat_Coeff_Slave.empty() && Lat_Coeff_Slave.type() != CV_64F) Lat_Coeff_Slave.convertTo(Lat_Coeff_Slave, CV_64F);
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "prf", tmp_double);
                if (!tmp_double.empty() && tmp_double.type() != CV_64F) {
                    tmp_double.convertTo(tmp_double, CV_64F);
                }
                interp_interval_slave = 1 / tmp_double.at<double>(0, 0);

                FC.read_str_from_h5(SAR_images.at(i).c_str(), "acquisition_start_time", time_slave_str);
                FC.utc2gps(time_slave_str.c_str(), &time_Slave);

                double delta = (time_Slave - time_Master) / 60 / 60 / 24;
                temporal_baseline.push_back(delta);

                util.baseline_estimation(State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
                    offset_row, offset_col, Rows, Cols, interp_interval, interp_interval_slave, &V_baseline, &H_baseline, &sigma_V, &sigma_H);

                spatial_baseline.push_back(V_baseline);
                cc.at<double>(0, i) = V_baseline;
                cc.at<double>(1, i) = delta;
            }
            catch (const std::exception& e) {
                emit errorProcess(QStringLiteral("处理从图像 %1 失败: ").arg(filePaths.at(i)) + QString::fromStdString(e.what()));
                return;
            }
            catch (...) {
                emit errorProcess(QStringLiteral("处理从图像 %1 时发生未知错误！").arg(filePaths.at(i)));
                return;
            }
        }
        emit updateProcess(20 + (i + 1) * 80 / image_number, QStringLiteral("正在计算时空基线……"));
    }

    emit sendBL(temporal_baseline, spatial_baseline, index);
    InSARLogManager::LogInfo("BaselineWorker", "Baseline Estimate task completed successfully.");
    emit endProcess();
}
