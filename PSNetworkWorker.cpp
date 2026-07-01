#include "PSNetworkWorker.h"
#include "FormatConversion.h"
#include "ComplexMat.h"
#include "PSI.h"
#include "Utils.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <cmath>

#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "PSI_d.lib")
#pragma comment(lib, "Utils_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "PSI.lib")
#pragma comment(lib, "Utils.lib")
#endif

PSNetworkWorker::PSNetworkWorker(QObject* parent)
    : BaseWorker(parent)
{
}

PSNetworkWorker::~PSNetworkWorker()
{
}

void PSNetworkWorker::build_network(
    double max_edge_length,
    int ref_row,
    int ref_col,
    QString projectPath,
    QString projectName,
    QString dstNode,
    QString candidatesH5,
    QStringList slcFilePaths
)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("PSNetworkWorker", QString("build_network started. Output Node: %1").arg(dstNode));

    if (slcFilePaths.isEmpty() || candidatesH5.isEmpty()) {
        emit errorProcess(QStringLiteral("输入文件路径为空"));
        return;
    }

    FormatConversion FC;
    cv::Mat ps_mask;

    // 1. 从 candidatesH5 读取 ps_mask
    int ret = FC.read_array_from_h5(candidatesH5.toStdString().c_str(), "ps_mask", ps_mask);
    if (ret != 0 || ps_mask.empty()) {
        emit errorProcess(QStringLiteral("读取 PS 候选点掩膜失败: ") + candidatesH5);
        return;
    }

    emit updateProcess(10, QStringLiteral("正在构建 Delaunay 三角网..."));

    // 2. 调用 DLL 构建网络
    PSI psi;
    std::vector<PS_Point> ps_points;
    std::vector<PS_Edge> edges;
    ret = psi.build_ps_network(ps_mask, ps_points, edges, max_edge_length);
    if (ret != 0 || ps_points.empty()) {
        emit errorProcess(QStringLiteral("DLL 三角网构建失败"));
        return;
    }

    int ps_count = static_cast<int>(ps_points.size());
    int edge_count = static_cast<int>(edges.size());
    InSARLogManager::LogInfo("PSNetworkWorker", QString("Network built. PS count: %1, Edge count: %2").arg(ps_count).arg(edge_count));

    // 3. 计算/获取参考点索引 (若未指定，则选择最接近图像中心的点)
    int ref_index = 0;
    int rows = ps_mask.rows;
    int cols = ps_mask.cols;
    int target_row = (ref_row == -1) ? rows / 2 : ref_row;
    int target_col = (ref_col == -1) ? cols / 2 : ref_col;
    double min_dist = 9999999.0;
    for (int i = 0; i < ps_count; ++i) {
        double dist = std::sqrt(std::pow(ps_points[i].row - target_row, 2) + std::pow(ps_points[i].col - target_col, 2));
        if (dist < min_dist) {
            min_dist = dist;
            ref_index = i;
        }
    }
    InSARLogManager::LogInfo("PSNetworkWorker", QString("Selected Reference Point Index: %1 at (row: %2, col: %3)")
        .arg(ref_index).arg(ps_points[ref_index].row).arg(ps_points[ref_index].col));

    // 4. 逐景影像流式读取并提取稀疏 PS 点复数值 (防 OOM 且优化 I/O)
    int num_images = slcFilePaths.size();
    cv::Mat ps_slc_data = cv::Mat::zeros(ps_count, num_images, CV_32FC2);

    for (int k = 0; k < num_images; ++k) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            emit errorProcess(QStringLiteral("用户中止了计算"));
            return;
        }

        emit updateProcess(20 + int(double(k) / num_images * 50), QStringLiteral("提取稀疏点数据: %1/%2").arg(k + 1).arg(num_images));
        
        QString slcPath = slcFilePaths.at(k);
        ComplexMat slc;
        ret = FC.read_slc_from_h5(slcPath.toStdString().c_str(), slc);
        if (ret != 0) {
            emit errorProcess(QStringLiteral("读取 SLC H5 文件失败: ") + slcPath);
            return;
        }

        cv::Mat slc_re = slc.re;
        cv::Mat slc_im = slc.im;
        if (slc_re.type() != CV_32F) slc_re.convertTo(slc_re, CV_32F);
        if (slc_im.type() != CV_32F) slc_im.convertTo(slc_im, CV_32F);

        #pragma omp parallel for schedule(static)
        for (int i = 0; i < ps_count; ++i) {
            int r = ps_points[i].row;
            int c = ps_points[i].col;
            float re_val = slc_re.at<float>(r, c);
            float im_val = slc_im.at<float>(r, c);
            ps_slc_data.at<cv::Vec2f>(i, k) = cv::Vec2f(re_val, im_val);
        }
    }

    emit updateProcess(75, QStringLiteral("估计时空基线与元数据读取中..."));

    // 5. 估计时空基线（默认选择第一景影像为主星）并读取核心元数据
    cv::Mat temporal_baseline = cv::Mat::zeros(num_images, 1, CV_32FC1);
    cv::Mat spatial_baseline = cv::Mat::zeros(num_images, 1, CV_32FC1);
    cv::Mat formation_matrix = cv::Mat::zeros(num_images - 1, 2, CV_32SC1); // 默认单主星网络星

    Utils util;
    // 载入主影像轨道参数
    Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
    Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
    double interp_interval_master = 0;
    int offset_row_master = 0, offset_col_master = 0;
    double time_Master = 0;
    string time_master_str;

    double carrier_frequency = 0.0;
    double inc_center = 0.0;
    double slant_range_first_pixel = 0.0;
    double range_spacing = 0.0;
    int offset_col = 0;

    QString masterPath = slcFilePaths.at(0); // 第一景作为 Master
    ret = FC.read_array_from_h5(masterPath.toStdString().c_str(), "state_vec", State_Vec_Master);
    ret += FC.read_array_from_h5(masterPath.toStdString().c_str(), "lon_coefficient", Lon_Coeff_Master);
    ret += FC.read_array_from_h5(masterPath.toStdString().c_str(), "lat_coefficient", Lat_Coeff_Master);
    ret += FC.read_array_from_h5(masterPath.toStdString().c_str(), "prf", tmp_double);
    interp_interval_master = 1.0 / tmp_double.at<double>(0, 0);

    Mat tmp = Mat::zeros(1, 1, CV_32SC1);
    FC.read_array_from_h5(masterPath.toStdString().c_str(), "offset_row", tmp);
    offset_row_master = tmp.at<int>(0, 0);
    FC.read_array_from_h5(masterPath.toStdString().c_str(), "offset_col", tmp);
    offset_col_master = tmp.at<int>(0, 0);
    offset_col = offset_col_master;

    FC.read_str_from_h5(masterPath.toStdString().c_str(), "acquisition_start_time", time_master_str);
    FC.utc2gps(time_master_str.c_str(), &time_Master);

    // 获取核心雷达几何参数
    FC.read_double_from_h5(masterPath.toStdString().c_str(), "carrier_frequency", &carrier_frequency);
    FC.read_double_from_h5(masterPath.toStdString().c_str(), "slant_range_first_pixel", &slant_range_first_pixel);
    FC.read_double_from_h5(masterPath.toStdString().c_str(), "range_spacing", &range_spacing);
    
    Mat tmp_inc;
    int ret_inc = FC.read_array_from_h5(masterPath.toStdString().c_str(), "inc_coefficient", tmp_inc);
    if (ret_inc == 0 && !tmp_inc.empty()) {
        inc_center = tmp_inc.at<double>(0, 0);
    } else {
        FC.read_double_from_h5(masterPath.toStdString().c_str(), "inc_center", &inc_center);
    }

    for (int i = 0; i < num_images; ++i) {
        if (i == 0) {
            temporal_baseline.at<float>(i) = 0.0f;
            spatial_baseline.at<float>(i) = 0.0f;
        } else {
            Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
            double interp_interval_slave = 0;
            double V_baseline = 0, H_baseline = 0;
            double sigma_V = 0, sigma_H = 0;
            double time_Slave = 0;
            string time_slave_str;

            QString slavePath = slcFilePaths.at(i);
            FC.read_array_from_h5(slavePath.toStdString().c_str(), "state_vec", State_Vec_Slave);
            FC.read_array_from_h5(slavePath.toStdString().c_str(), "lon_coefficient", Lon_Coeff_Slave);
            FC.read_array_from_h5(slavePath.toStdString().c_str(), "lat_coefficient", Lat_Coeff_Slave);
            FC.read_array_from_h5(slavePath.toStdString().c_str(), "prf", tmp_double);
            interp_interval_slave = 1.0 / tmp_double.at<double>(0, 0);

            FC.read_str_from_h5(slavePath.toStdString().c_str(), "acquisition_start_time", time_slave_str);
            FC.utc2gps(time_slave_str.c_str(), &time_Slave);

            double delta_t = (time_Slave - time_Master) / 60.0 / 60.0 / 24.0;
            temporal_baseline.at<float>(i) = static_cast<float>(delta_t);

            util.baseline_estimation(
                State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
                offset_row_master, offset_col_master, rows, cols, interp_interval_master, interp_interval_slave,
                &V_baseline, &H_baseline, &sigma_V, &sigma_H
            );
            spatial_baseline.at<float>(i) = static_cast<float>(V_baseline);

            // 填充干涉组合矩阵：主星为0，从星为i
            formation_matrix.at<int>(i - 1, 0) = 0;
            formation_matrix.at<int>(i - 1, 1) = i;
        }
    }

    emit updateProcess(85, QStringLiteral("计算边相位差时序中..."));

    // 6. 调用 DLL 计算边相位差
    cv::Mat edge_phase_diff;
    ret = psi.compute_ps_phase_diff(ps_points, edges, ps_slc_data, formation_matrix, edge_phase_diff);
    if (ret != 0) {
        emit errorProcess(QStringLiteral("DLL 计算边相位差失败"));
        return;
    }

    emit updateProcess(90, QStringLiteral("正在写入 PS_network.h5 成果..."));

    // 7. 保存网络成果
    QString rawPath = projectPath;
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString outDir = dir + "/" + dstNode;
    QDir().mkpath(outDir);

    QString h5Path = outDir + "/PS_network.h5";
    
    // 准备点集坐标矩阵
    cv::Mat ps_coords(ps_count, 2, CV_32SC1);
    for (int i = 0; i < ps_count; ++i) {
        ps_coords.at<int>(i, 0) = ps_points[i].row;
        ps_coords.at<int>(i, 1) = ps_points[i].col;
    }

    // 准备边端点关系矩阵
    cv::Mat edge_nodes(edge_count, 2, CV_32SC1);
    for (int i = 0; i < edge_count; ++i) {
        edge_nodes.at<int>(i, 0) = edges[i].end1;
        edge_nodes.at<int>(i, 1) = edges[i].end2;
    }

    ret = FC.write_array_to_h5(h5Path.toStdString().c_str(), "ps_coordinates", ps_coords);
    ret += FC.write_array_to_h5(h5Path.toStdString().c_str(), "edges", edge_nodes);
    ret += FC.write_array_to_h5(h5Path.toStdString().c_str(), "edge_phase_diff", edge_phase_diff);
    ret += FC.write_array_to_h5(h5Path.toStdString().c_str(), "temporal_baseline", temporal_baseline);
    ret += FC.write_array_to_h5(h5Path.toStdString().c_str(), "spatial_baseline", spatial_baseline);
    ret += FC.write_array_to_h5(h5Path.toStdString().c_str(), "formation_matrix", formation_matrix);
    
    ret += FC.write_int_to_h5(h5Path.toStdString().c_str(), "ps_count", ps_count);
    ret += FC.write_int_to_h5(h5Path.toStdString().c_str(), "edge_count", edge_count);
    ret += FC.write_int_to_h5(h5Path.toStdString().c_str(), "ref_index", ref_index);

    // 写入雷达元数据
    ret += FC.write_double_to_h5(h5Path.toStdString().c_str(), "carrier_frequency", carrier_frequency);
    ret += FC.write_double_to_h5(h5Path.toStdString().c_str(), "inc_center", inc_center);
    ret += FC.write_double_to_h5(h5Path.toStdString().c_str(), "slant_range_first_pixel", slant_range_first_pixel);
    ret += FC.write_double_to_h5(h5Path.toStdString().c_str(), "range_spacing", range_spacing);
    ret += FC.write_int_to_h5(h5Path.toStdString().c_str(), "offset_col", offset_col);
    ret += FC.write_int_to_h5(h5Path.toStdString().c_str(), "rows", rows);
    ret += FC.write_int_to_h5(h5Path.toStdString().c_str(), "cols", cols);

    if (ret != 0) {
        emit errorProcess(QStringLiteral("写入 PS_network.h5 失败"));
        return;
    }

    emit updateProcess(100, QStringLiteral("三角网构建完成"));
    emit endProcess();
}
