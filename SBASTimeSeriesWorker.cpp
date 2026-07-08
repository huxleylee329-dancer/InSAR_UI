#include "SBASTimeSeriesWorker.h"
#include "NodeUtils.h"
#include <Unwrap.h>
#include "SBAS.h"
#include "Utils.h"
#include "FormatConversion.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <QThread>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QMessageBox>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

SBASTimeSeriesWorker::SBASTimeSeriesWorker(QObject* parent)
    : BaseWorker(parent)
{
}

SBASTimeSeriesWorker::~SBASTimeSeriesWorker()
{
}

void SBASTimeSeriesWorker::SBAS_time_series(double temporal_thresh_low, double temporal_thresh, double spatial_thresh,
                                            int multilook_rg, int multilook_az, int unwrap_method, double alpha,
                                            double coherence_thresh, double temporal_coherence_thresh,
                                            double refinement_coh_thresh, double refinemen_def_thresh,
                                            QString projectPath, QString projectName, QString dstNode, QString csvPath,
                                            QStringList filePaths, QStandardItemModel* model)
{
    /*创建csv文件*/
    QDir csv(csvPath);
    if (!csv.exists())
    {
        if (!csv.mkpath(csv.absolutePath()))
        {
            InSARLogManager::LogWarning("UI", QStringLiteral("创建csv文件失败，请检查路径是否正确!"));
            emit errorProcess(QStringLiteral("创建csv文件失败，请检查路径是否正确!"));
            return;
        }
    }
    QFile csv_file(csvPath);
    QTextStream in(&csv_file);
    if (!csv_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {    
        InSARLogManager::LogWarning("UI", QStringLiteral("打开csv文件失败，请检查路径是否正确!"));
        emit errorProcess(QStringLiteral("打开csv文件失败，请检查路径是否正确!"));
        return;
    }

    NodeUtils::Hdf5Locker locker;
    Utils util; SBAS sbas; FormatConversion conversion; Unwrap unwrap;
    int ret;
    vector<string> SAR_images;
    for (const QString& path : filePaths) {
        SAR_images.push_back(path.toStdString());
    }
    
    int image_number = SAR_images.size();
    if (image_number == 0) {
        emit errorProcess(QStringLiteral("无输入图像数据！"));
        return;
    }

    QString save_path = projectPath;
    string save_path_std_string = save_path.toStdString();
    std::replace(save_path_std_string.begin(), save_path_std_string.end(), '/', '\\');

    //确定应用程序路径
    string appPath = QCoreApplication::applicationDirPath().toStdString();
    std::replace(appPath.begin(), appPath.end(), '/', '\\');

    /*干涉相位生成*/
    emit updateProcess(10, QStringLiteral("差分干涉相位生成……"));
    Mat temporal, spatial, formation_matrix, spatial_baseline, temporal_baseline;
    util.spatialTemporalBaselineEstimation(SAR_images, 1, temporal, spatial);
    sbas.get_formation_matrix(spatial, temporal, spatial_thresh, temporal_thresh_low, temporal_thresh / 365.0,
        formation_matrix, spatial_baseline, temporal_baseline);
    QString ifgSavePath = save_path + "/" + dstNode;
    string path1 = ifgSavePath.toStdString();
    std::replace(path1.begin(), path1.end(), '/', '\\');
    QDir dir(save_path);
    if (!dir.exists(dstNode)) dir.mkdir(dstNode);
    sbas.generate_interferograms(SAR_images, formation_matrix, spatial_baseline, temporal_baseline, multilook_az, multilook_rg,
        path1.c_str(), true, alpha);

    /*计算高相干点*/
    vector<SBAS_edge> edges;
    vector<SBAS_node> nodes;
    vector<SBAS_triangle> triangles;
    vector<int> node_neighbours;
    vector<string> phaseFiles;
    int n_images = formation_matrix.rows;
    char str[256];
    for (int i = 0; i < n_images; i++)
    {
        for (int j = 0; j < i; j++)
        {
            if (formation_matrix.at<int>(i, j) == 1)
            {
                memset(str, 0, 256);
                sprintf(str, "\\%d_%d.h5", i + 1, j + 1);
                string str2 = path1 + str;
                phaseFiles.push_back(str2);
            }
        }
    }
    Mat mask; 
    Mat coherence, phase;
    string mcf_problem = path1 + "\\mcf_problem.net";
    string mcf_solution = path1 + "\\mcf_problem.net.sol";
    if (unwrap_method == 1)
    {
        /*生成高相干三角网络*/
        sbas.generate_high_coherence_mask(phaseFiles, 3, 3, coherence_thresh, 0.5, mask);
        int nonzero = cv::countNonZero(mask);
        string node_file = path1 + "\\high_coherence.node";
        string edge_file = path1 + "\\high_coherence.1.edge";
        string ele_file = path1 + "\\high_coherence.1.ele";
        string neigh_file = path1 + "\\high_coherence.1.neigh";
        sbas.write_high_coherence_node(mask, node_file.c_str());
        util.gen_delaunay(node_file.c_str(), appPath.c_str());
        sbas.read_edges(edge_file.c_str(), nonzero, edges, node_neighbours);
        sbas.init_SBAS_node(nodes, edges, node_neighbours);
        sbas.init_SBAS_triangle(ele_file.c_str(), neigh_file.c_str(), triangles, edges, nodes);
        sbas.set_high_coherence_node_coordinate(mask, nodes);

        double obj;
        //三角网络解缠
        for (int i = 0; i < phaseFiles.size(); i++)
        {
            if (QThread::currentThread()->isInterruptionRequested()) {
                InSARLogManager::LogInfo("SBASTimeSeriesWorker", "Task interrupted during unwrap.");
                return;
            }
            QString pFile = QString::fromStdString(phaseFiles[i]);
            NodeUtils::readMatFromH5(pFile, "phase", phase, CV_64F);
            ret = NodeUtils::readMatFromH5(pFile, "coherence", coherence, CV_64F) ? 0 : -1;
            if (ret < 0)
            {
                util.phase_coherence(phase, coherence);
            }
            sbas.set_high_coherence_node_phase(mask, nodes, edges, phase);
            sbas.set_weight_by_coherence(coherence, nodes, edges);
            sbas.compute_high_coherence_residue(nodes, edges, triangles);
            int num_residues = 0;
            sbas.residue_num(triangles, &num_residues);
            if (num_residues > 0)
            {
                sbas.writeDIMACS_spatial(mcf_problem.c_str(), nodes, edges, triangles);
                unwrap.mcf_delaunay(mcf_problem.c_str(), appPath.c_str());
                sbas.readDIMACS(mcf_solution.c_str(), nodes, edges, triangles, obj);
            }
            sbas.floodFillUnwrap(nodes, edges, 1, false);
            sbas.retrieve_unwrapped_phase(nodes, phase);
            if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
            conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase);
            for (int j = 0; j < nodes.size(); j++)
            {
                nodes[j].b_unwrapped = false;
            }
            int process = double(i + 1) / phaseFiles.size() * 100.0 * 0.5;
            emit updateProcess(10 + process, QStringLiteral("相位解缠中……"));
        }
    }
    else
    {
        //规则网络解缠
        mask = 1;
        for (int i = 0; i < phaseFiles.size(); i++)
        {
            if (QThread::currentThread()->isInterruptionRequested()) {
                InSARLogManager::LogInfo("SBASTimeSeriesWorker", "Task interrupted during unwrap.");
                return;
            }
            QString pFile = QString::fromStdString(phaseFiles[i]);
            NodeUtils::readMatFromH5(pFile, "phase", phase, CV_64F);
            ret = NodeUtils::readMatFromH5(pFile, "coherence", coherence, CV_64F) ? 0 : -1;
            if (ret < 0)
            {
                util.phase_coherence(phase, coherence);
            }
            Mat residue, phase2;
            util.residue(phase, residue);
            if (unwrap_method == 2)//SNAPHU方法
            {
                unwrap.snaphu(phase, phase2, path1.c_str());
            }
            else//MCF方法
            {
                unwrap.MCF(phase, phase2, coherence, residue, mcf_problem.c_str(), appPath.c_str());
            }
            if (phase2.type() != CV_32F) phase2.convertTo(phase2, CV_32F);
            conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase2);
            int process = double(i + 1) / phaseFiles.size() * 100.0 * 0.5;
            emit updateProcess(10 + process, QStringLiteral("相位解缠中……"));
        }
    }

    if (QThread::currentThread()->isInterruptionRequested()) return;

    /*第一次轨道精炼和重去平*/
    emit updateProcess(65, QStringLiteral("轨道精炼和重去平……"));
    int ref_i = 0, ref_j = 0;
    bool b_break = false;
    for (int i = 0; i < phase.rows; i++)
    {
        for (int j = 0; j < phase.cols; j++)
        {
            if (mask.at<int>(i, j) == 1)
            {
                ref_i = i; ref_j = j;
                b_break = true;
                break;
            }
        }
        if (b_break) break;
    }
    for (int i = 0; i < phaseFiles.size(); i++)
    {
        QString pFile = QString::fromStdString(phaseFiles[i]);
        NodeUtils::readMatFromH5(pFile, "unwrapped_phase_1", phase, CV_64F);
        NodeUtils::readMatFromH5(pFile, "coherence", coherence, CV_64F);
        sbas.refinement_and_reflattening(phase, mask, coherence, refinement_coh_thresh);
        phase = phase - phase.at<double>(ref_i, ref_j);
        if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
        conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase);
    }

    if (QThread::currentThread()->isInterruptionRequested()) return;

    /*最小二乘法求解线性形变速率和高程残差*/
    int M = phaseFiles.size();
    int N = SAR_images.size() - 1;
    Mat B(M, N, CV_64F); B = 0.0;
    Mat one = Mat::ones(N, 1, CV_64F);
    for (int i = 0; i < M; i++)
    {
        int ii, jj;
        string temp_str = phaseFiles[i];
        std::replace(temp_str.begin(), temp_str.end(), '\\', '/');
        QFileInfo fileinfo(QString(temp_str.c_str()));
        sscanf(fileinfo.baseName().toStdString().c_str(), "%d_%d", &ii, &jj);
        for (int j = jj; j < ii; j++)
        {
            B.at<double>(i, j - 1) = (temporal.at<double>(0, j) - temporal.at<double>(0, j - 1)) / 365.0;
        }
    }
    Mat B1 = B * one;
    Mat c(M, 1, CV_64F), col(nodes.size(), 1, CV_64F); c = 0.0; col = 0.0;
    vector<Mat> phase_vec, phase_vec2, coh_vec;
    phase_vec.resize(M); phase_vec2.resize(N + 1); coh_vec.resize(M);

    int offset_col_val, row, count_val = 0;
    double nearRange, theta = 32.412, spacing, wavelength, B_spatial, B_temporal;
    for (int i = 0; i < M; i++)
    {
        Mat temp;
        QString pFile = QString::fromStdString(phaseFiles[i]);
        NodeUtils::readScalarFromH5(pFile, "offset_col", offset_col_val);
        NodeUtils::readScalarFromH5(pFile, "slant_range_first_pixel", nearRange);
        NodeUtils::readScalarFromH5(pFile, "range_spacing", spacing);
        NodeUtils::readScalarFromH5(pFile, "B_spatial", B_spatial);
        NodeUtils::readScalarFromH5(pFile, "B_temporal", B_temporal);
        NodeUtils::readScalarFromH5(pFile, "carrier_frequency", wavelength);
        wavelength = VEL_C / wavelength;
        if (NodeUtils::readMatFromH5(pFile, "inc_coefficient", temp, CV_64F)) {
            theta = temp.at<double>(0, 0) / 180.0 * PI;
        }
        else
        {
            NodeUtils::readScalarFromH5(pFile, "inc_center", theta);
            theta = theta / 180.0 * PI;
        }
        double r = nearRange + double(offset_col_val) * spacing;
        c.at<double>(i, 0) = 4 * PI / wavelength * B_spatial / sin(theta) / r;
        NodeUtils::readMatFromH5(pFile, "unwrapped_phase_2", phase_vec[i], CV_64F);
        NodeUtils::readMatFromH5(pFile, "coherence", coh_vec[i], CV_64F);
    }
    Mat dummy = Mat::zeros(phase.rows, phase.cols, CV_64F);
    for (int i = 0; i < N + 1; i++)
    {
        dummy.copyTo(phase_vec2[i]);
    }
    Mat BMc;
    Mat v(phase.rows, phase.cols, CV_64F), z(phase.rows, phase.cols, CV_64F), temporal_coh(phase.rows, phase.cols, CV_64F);
    v = 0.0; z = 0.0; temporal_coh = 0.0;
    cv::hconcat(B1, c, BMc);
    
    emit updateProcess(70, QStringLiteral("时间序列分析(1/2)……"));
    std::atomic<int> completed_rows(0);
    std::atomic<bool> cancel_flag(false);
    std::atomic<int> max_reported_pct(70);
    int step_val = std::max(1, phase.rows / 10);

#pragma omp parallel for schedule(guided)
    for (int i = 0; i < phase.rows; i++)
    {
        if (cancel_flag || QThread::currentThread()->isInterruptionRequested()) {
            cancel_flag = true;
            continue;
        }

        Mat temp(M, 1, CV_64F), temp_coh(M, M, CV_64F); temp = 0.0, temp_coh = 0.0;
        for (int j = 0; j < phase.cols; j++)
        {
            if (mask.at<int>(i, j) > 0)
            {
                for (int k = 0; k < M; k++)
                {
                    temp.at<double>(k, 0) = phase_vec[k].at<double>(i, j);
                    temp_coh.at<double>(k, k) = coh_vec[k].at<double>(i, j);
                }
                Mat A_t, A, b;
                BMc.copyTo(A);
                temp.copyTo(b);
                cv::transpose(A, A_t);
                A = A_t * temp_coh * A;
                b = A_t * temp_coh * b;
                Mat x;
                if (!cv::solve(A, b, x, cv::DECOMP_LU))
                {
                    // can't solve
                }
                else
                {
                    v.at<double>(i, j) = x.at<double>(0, 0);
                    z.at<double>(i, j) = x.at<double>(1, 0);
                }
            }
        }

        int current_completed = ++completed_rows;
        if (current_completed % step_val == 0) {
            int progress = 70 + (current_completed * 5 / phase.rows);
            int prev = max_reported_pct.load();
            while (progress > prev && !max_reported_pct.compare_exchange_weak(prev, progress)) {
                // Keep trying
            }
            if (progress > prev) {
                #pragma omp critical(sbas_ts_progress_1)
                {
                    emit updateProcess(progress, QStringLiteral("时间序列分析(1/2)……"));
                }
            }
        }
    }

    if (cancel_flag || QThread::currentThread()->isInterruptionRequested()) return;

    /*第二次轨道精炼和重去平*/
    v = v / 4 / PI * wavelength;
    Mat refinement_mask; mask.copyTo(refinement_mask); refinement_mask = 0;
    for (int i = 0; i < phase.rows; i++)
    {
        for (int j = 0; j < phase.cols; j++)
        {
            if (mask.at<int>(i, j) == 1 && fabs(v.at<double>(i, j)) < refinemen_def_thresh)
            {
                refinement_mask.at<int>(i, j) = 1;
            }
        }
    }
    emit updateProcess(75, QStringLiteral("第二次轨道精炼和重去平……"));
    
    for (int i = 0; i < phaseFiles.size(); i++)
    {
        if (cv::countNonZero(refinement_mask) < 4)
        {
            QString pFile = QString::fromStdString(phaseFiles[i]);
            NodeUtils::readMatFromH5(pFile, "unwrapped_phase_2", phase_vec[i], CV_64F);
        }
        else
        {
            b_break = false;
            for (int r_idx = 0; r_idx < phase.rows; r_idx++)
            {
                for (int c_idx = 0; c_idx < phase.cols; c_idx++)
                {
                    if (refinement_mask.at<int>(r_idx, c_idx) == 1)
                    {
                        ref_i = r_idx; ref_j = c_idx;
                        b_break = true;
                        break;
                    }
                }
                if (b_break) break;
            }
            QString pFile = QString::fromStdString(phaseFiles[i]);
            NodeUtils::readMatFromH5(pFile, "unwrapped_phase_1", phase, CV_64F);
            NodeUtils::readMatFromH5(pFile, "coherence", coherence, CV_64F);
            sbas.refinement_and_reflattening(phase, refinement_mask, coherence, refinement_coh_thresh);
            phase = phase - phase.at<double>(ref_i, ref_j);
            phase.copyTo(phase_vec[i]);
            if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
            conversion.write_subarray_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase, 0, 0, phase.rows, phase.cols);
        }
    }

    if (QThread::currentThread()->isInterruptionRequested()) return;

    emit updateProcess(75, QStringLiteral("时间序列分析(2/2)……"));
    std::atomic<int> completed_rows2(0);
    std::atomic<bool> cancel_flag2(false);
    std::atomic<int> max_reported_pct2(75);
    int step_val2 = std::max(1, phase.rows / 10);

#pragma omp parallel for schedule(guided)
    for (int i = 0; i < phase.rows; i++)
    {
        if (cancel_flag2 || QThread::currentThread()->isInterruptionRequested()) {
            cancel_flag2 = true;
            continue;
        }

        Mat temp(M, 1, CV_64F), temp_coh(M, M, CV_64F); temp = 0.0, temp_coh = 0.0;
        for (int j = 0; j < phase.cols; j++)
        {
            if (mask.at<int>(i, j) > 0)
            {
                for (int k = 0; k < M; k++)
                {
                    temp.at<double>(k, 0) = phase_vec[k].at<double>(i, j);
                    temp_coh.at<double>(k, k) = coh_vec[k].at<double>(i, j);
                }
                Mat A_t, A, b;
                BMc.copyTo(A);
                temp.copyTo(b);
                cv::transpose(A, A_t);
                A = A_t * temp_coh * A;
                b = A_t * temp_coh * b;
                Mat x;
                if (!cv::solve(A, b, x, cv::DECOMP_LU))
                {
                    // can't solve
                }
                else
                {
                    v.at<double>(i, j) = x.at<double>(0, 0);
                    z.at<double>(i, j) = x.at<double>(1, 0);
                }
                temp = temp - x.at<double>(1, 0) * c;
                B.copyTo(A);
                temp.copyTo(b);
                cv::transpose(A, A_t);
                A = A_t * temp_coh * A;
                b = A_t * temp_coh * b;
                if (!cv::solve(A, b, x, cv::DECOMP_SVD))
                {
                    // can't solve SVD
                }
                else
                {
                    for (int k = 1; k < N + 1; k++)
                    {
                        phase_vec2[k].at<double>(i, j) = x.at<double>(k - 1, 0) *
                            (temporal.at<double>(0, k) - temporal.at<double>(0, k - 1)) / 365.0
                            + phase_vec2[k - 1].at<double>(i, j);
                    }
                }
                x = B * x;
                double coh = 0.0;
                sbas.compute_temporal_coherence(x, temp, &coh);
                temporal_coh.at<double>(i, j) = coh;
            }
        }

        int current_completed = ++completed_rows2;
        if (current_completed % step_val2 == 0) {
            int progress = 75 + (current_completed * 5 / phase.rows);
            int prev = max_reported_pct2.load();
            while (progress > prev && !max_reported_pct2.compare_exchange_weak(prev, progress)) {
                // Keep trying
            }
            if (progress > prev) {
                #pragma omp critical(sbas_ts_progress_2)
                {
                    emit updateProcess(progress, QStringLiteral("时间序列分析(2/2)……"));
                }
            }
        }
    }

    if (cancel_flag2 || QThread::currentThread()->isInterruptionRequested()) return;

    //保存时序分析结果
    emit updateProcess(80, QStringLiteral("结果筛选……"));
    Mat out_mask, mask_count_map;
    mask.copyTo(out_mask);
    mask.copyTo(mask_count_map);
    out_mask = 0; mask_count_map = 0;
    string times_series_h5 = path1 + "\\SBAS_time_series.h5";
    conversion.creat_new_h5(times_series_h5.c_str());
    int nr, nc;
    nr = mask.rows; nc = mask.cols;
    int valide_count = 0;
    for (int i = 0; i < nr; i++)
    {
        for (int j = 0; j < nc; j++)
        {
            if (temporal_coh.at<double>(i, j) > temporal_coherence_thresh) 
            { 
                out_mask.at<int>(i, j) = 1; 
                mask_count_map.at<int>(i, j) = valide_count;
                valide_count++;
            }
        }
    }
    if (valide_count == 0)
    {
        valide_count = cv::countNonZero(mask);
        mask.copyTo(out_mask);
        int count_temp = 0;
        for (int i = 0; i < nr; i++)
        {
            for (int j = 0; j < nc; j++)
            {
                if (out_mask.at<int>(i, j) == 1)
                {
                    mask_count_map.at<int>(i, j) = count_temp;
                    count_temp++;
                }
            }
        }
    }
    Mat time_series(valide_count, N + 1, CV_64F); time_series = 0.0; valide_count = 0;
    
    for (int i = 0; i < nr; i++)
    {
        for (int j = 0; j < nc; j++)
        {
            if (out_mask.at<int>(i, j) == 1)
            {
                in << qSetFieldWidth(6) << i << j;
                Mat series(1, N + 1, CV_64F); Mat temp_A(N + 1, 2, CV_64F); temp_A = 1.0; Mat temp_b(N + 1, 1, CV_64F);
                for (int k = 0; k < N + 1; k++)
                {
                    series.at<double>(0, k) = phase_vec2[k].at<double>(i, j);
                    temp_A.at<double>(k, 0) = temporal.at<double>(0, k) / 365.0;
                    temp_b.at<double>(k, 0) = phase_vec2[k].at<double>(i, j);
                    in << qSetFieldWidth(6) << qSetRealNumberPrecision(5) << series.at<double>(0, k);
                }
                series.copyTo(time_series(cv::Range(valide_count, valide_count + 1), cv::Range(0, N + 1)));
                valide_count++;
                Mat temp_A_t, temp_x;
                cv::transpose(temp_A, temp_A_t);
                temp_A = temp_A_t * temp_A;
                temp_b = temp_A_t * temp_b;
                if (cv::solve(temp_A, temp_b, temp_x, cv::DECOMP_LU))
                {
                    v.at<double>(i, j) = temp_x.at<double>(0, 0);
                    in << qSetFieldWidth(6) << qSetRealNumberPrecision(5) << v.at<double>(i, j);
                }
                in << "\n";
            }
        }
    }
    csv_file.close();
    
    if (QThread::currentThread()->isInterruptionRequested()) return;

    emit updateProcess(95, QStringLiteral("结果保存……"));
    Mat mapped_lat, mapped_lon;
    double max_def, min_def;
    
    conversion.write_int_to_h5(times_series_h5.c_str(), "ref_row", ref_i);
    conversion.write_int_to_h5(times_series_h5.c_str(), "ref_col", ref_j);
    conversion.write_int_to_h5(times_series_h5.c_str(), "multilook_rg", multilook_rg);
    conversion.write_int_to_h5(times_series_h5.c_str(), "multilook_az", multilook_az);
    conversion.write_array_to_h5(times_series_h5.c_str(), "temporal_baseline", temporal);
    conversion.write_array_to_h5(times_series_h5.c_str(), "spatial_baseline", spatial);
    conversion.write_array_to_h5(times_series_h5.c_str(), "formation_matrix", formation_matrix);
    conversion.write_array_to_h5(times_series_h5.c_str(), "mask", out_mask);
    conversion.write_array_to_h5(times_series_h5.c_str(), "mask_count_map", mask_count_map);
    time_series = time_series / 4 / PI * wavelength;
    cv::minMaxLoc(time_series, &min_def, &max_def);
    conversion.write_double_to_h5(times_series_h5.c_str(), "max_deformation", max_def);
    conversion.write_double_to_h5(times_series_h5.c_str(), "min_deformation", min_def);
    conversion.write_array_to_h5(times_series_h5.c_str(), "deformation_time_series", time_series);
    conversion.write_array_to_h5(times_series_h5.c_str(), "temporal_coherence", temporal_coh);
    v = v / 4 / PI * wavelength;
    conversion.write_array_to_h5(times_series_h5.c_str(), "defomation_velocity", v);
    conversion.write_array_to_h5(times_series_h5.c_str(), "residue_topography", z);
    for (int ii = 0; ii < phaseFiles.size(); ii++)
    {
        QString pFile = QString::fromStdString(phaseFiles[ii]);
        if (NodeUtils::readMatFromH5(pFile, "mapped_lat", mapped_lat))
        {
            NodeUtils::readMatFromH5(pFile, "mapped_lon", mapped_lon);
            Mat lon_new(temporal_coh.rows, temporal_coh.cols, CV_32F), lat_new(temporal_coh.rows, temporal_coh.cols, CV_32F);
            for (int r_idx = 0; r_idx < temporal_coh.rows; r_idx++)
            {
                for (int c_idx = 0; c_idx < temporal_coh.cols; c_idx++)
                {
                    lon_new.at<float>(r_idx, c_idx) = cv::mean(mapped_lon(cv::Range(r_idx * multilook_az, r_idx * multilook_az + multilook_az),
                        cv::Range(c_idx * multilook_rg, c_idx * multilook_rg + multilook_rg)))[0];
                    lat_new.at<float>(r_idx, c_idx) = cv::mean(mapped_lat(cv::Range(r_idx * multilook_az, r_idx * multilook_az + multilook_az),
                        cv::Range(c_idx * multilook_rg, c_idx * multilook_rg + multilook_rg)))[0];
                }
            }
            conversion.write_array_to_h5(times_series_h5.c_str(), "mapped_lat", lat_new);
            conversion.write_array_to_h5(times_series_h5.c_str(), "mapped_lon", lon_new);
            break;
        }
    }
    
    // 如果有传入 QStandardItemModel，则是旧版 Workspace UI 弹窗在调用，我们需要更新项目树
    if (model)
    {
        QString times_series_h5_forward = QString::fromStdString(times_series_h5).replace('\\', '/');
        QMetaObject::invokeMethod(model, [=]() {
            QList<QStandardItem*> foundProjects = model->findItems(projectName);
            if (foundProjects.isEmpty()) return;
            QStandardItem* project = foundProjects[0];

            QStandardItem* SBAS_series = NULL;
            for (int i = 0; i < project->rowCount(); i++)
            {
                if (project->child(i, 0)->text() == dstNode)
                {
                    SBAS_series = project->child(i, 0);
                    break;
                }
            }

            if (!SBAS_series)
            {
                SBAS_series = new QStandardItem(dstNode);
                SBAS_series->setToolTip(projectName);
                int insert = 0;
                for (; insert < project->rowCount(); insert++)
                {
                    if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                        project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                        project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
                        project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
                        project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
                        project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
                        project->child(insert, 1)->text().compare("phase-3.0") == 0 ||
                        project->child(insert, 1)->text().compare("dem-1.0") == 0 ||
                        project->child(insert, 1)->text().compare("SBAS-1.0") == 0
                        )
                        continue;
                    else
                        break;
                }
                SBAS_series->setIcon(QIcon(FOLDER_ICON));
                project->insertRow(insert, SBAS_series);
                QStandardItem* SBAS_series_Rank = new QStandardItem("SBAS-1.0");
                project->setChild(insert, 1, SBAS_series_Rank);
            }

            QStandardItem* item_img = NULL;
            for (int j = 0; j < SBAS_series->rowCount(); j++)
            {
                if (SBAS_series->child(j, 0)->text() == "SBAS_time_series")
                {
                    item_img = SBAS_series->child(j, 0);
                    break;
                }
            }

            if (!item_img)
            {
                QStandardItem* SBAS_series_name = new QStandardItem(QString("SBAS_time_series"));
                SBAS_series_name->setToolTip("SBAS");
                QStandardItem* SBAS_series_name_path = new QStandardItem(times_series_h5_forward);
                SBAS_series_name->setIcon(QIcon(IMAGEDATA_ICON));
                SBAS_series->appendRow(SBAS_series_name);
                SBAS_series->setChild(SBAS_series->rowCount() - 1, 1, SBAS_series_name_path);

                /*写入XML*/
                XMLFile xmlfile;
                xmlfile.XMLFile_load((save_path + "/" + projectName).toStdString().c_str());
                QString relativePath = QString("/%1/SBAS_time_series.h5").arg(dstNode);
                xmlfile.XMLFile_add_SBAS(dstNode.toStdString().c_str(), "SBAS_time_series", relativePath.toStdString().c_str());
                xmlfile.XMLFile_save((save_path + "/" + projectName).toStdString().c_str());
            }
            else
            {
                SBAS_series->setChild(item_img->row(), 1, new QStandardItem(times_series_h5_forward));
            }
        }, Qt::BlockingQueuedConnection);
        emit sendModel(model);
    }
    else
    {
        /* 仅在工作流节点执行时，直接向XML写入记录 */
        XMLFile xmlfile;
        xmlfile.XMLFile_load((save_path + "/" + projectName).toStdString().c_str());
        QString relativePath = QString("/%1/SBAS_time_series.h5").arg(dstNode);
        xmlfile.XMLFile_add_SBAS(dstNode.toStdString().c_str(), "SBAS_time_series", relativePath.toStdString().c_str());
        xmlfile.XMLFile_save((save_path + "/" + projectName).toStdString().c_str());
    }

    InSARLogManager::LogInfo("SBASTimeSeriesWorker", "SBAS Time Series analysis completed successfully.");
    emit endProcess();
}
