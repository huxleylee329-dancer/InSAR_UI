#include "SBASReferenceReselectionWorker.h"
#include "NodeUtils.h"
#include <Unwrap.h>
#include "SBAS.h"
#include "Utils.h"
#include "FormatConversion.h"
#include "InSARLogManager.h"
#include <QThread>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <vector>
#include <string>
#include <algorithm>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

SBASReferenceReselectionWorker::SBASReferenceReselectionWorker(QObject* parent)
    : BaseWorker(parent)
{
}

SBASReferenceReselectionWorker::~SBASReferenceReselectionWorker()
{
}

void SBASReferenceReselectionWorker::SBAS_reference_reselection(QString save_path, QString srcNode, QString times_series_h5,
                                                                int ref_row, int ref_col, QList<QPoint> GCPs)
{
    NodeUtils::Hdf5Locker locker;
    Utils util; SBAS sbas; FormatConversion conversion;
    int ret;
    string times_series_h5_std = times_series_h5.toStdString();
    
    Mat formation_matrix, mask, temporal_baseline, reflattening_mask;
    ret = conversion.read_array_from_h5(times_series_h5_std.c_str(), "formation_matrix", formation_matrix);
    ret = conversion.read_array_from_h5(times_series_h5_std.c_str(), "mask", mask);
    ret = conversion.read_array_from_h5(times_series_h5_std.c_str(), "temporal_baseline", temporal_baseline);
    if (!temporal_baseline.empty() && temporal_baseline.type() != CV_64F) {
        temporal_baseline.convertTo(temporal_baseline, CV_64F);
    }
    
    //确定应用程序路径
    string appPath = QCoreApplication::applicationDirPath().toStdString();
    std::replace(appPath.begin(), appPath.end(), '/', '\\');
    QString ifgSavePath = save_path + "/" + srcNode;
    string path1 = ifgSavePath.toStdString();
    std::replace(path1.begin(), path1.end(), '/', '\\');

    int num_GCPs = GCPs.size();
    mask.copyTo(reflattening_mask); reflattening_mask = 0;
    for (int i = 0; i < num_GCPs; i++)
    {
        reflattening_mask.at<int>(GCPs[i].x(), GCPs[i].y()) = 1;
    }

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
    
    Mat coherence, phase;
    /*轨道精炼和重去平*/
    emit updateProcess(10, QStringLiteral("轨道精炼和重去平……"));
    for (int i = 0; i < phaseFiles.size(); i++)
    {
        if (QThread::currentThread()->isInterruptionRequested())
        {
            emit errorProcess(QStringLiteral("任务被中断"));
            return;
        }
        conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase);
        if (phase.type() != CV_64F) phase.convertTo(phase, CV_64F);
        conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coherence);
        if (coherence.type() != CV_64F) coherence.convertTo(coherence, CV_64F);
        sbas.refinement_and_reflattening(phase, reflattening_mask, coherence, 0.0);
        phase = phase - phase.at<double>(ref_row, ref_col);
        if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
        conversion.write_subarray_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase, 0, 0, phase.rows, phase.cols);
    }

    /*最小二乘法求解线性形变速率和高程残差*/
    //首先确定矩阵B
    int M = phaseFiles.size(); //干涉图幅数
    int N = temporal_baseline.cols - 1; //时间序列数
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
            B.at<double>(i, j - 1) = (temporal_baseline.at<double>(0, j) - temporal_baseline.at<double>(0, j - 1)) / 365.0;
        }
    }
    Mat B1 = B * one;
    //确定矩阵c
    Mat c(M, 1, CV_64F); c = 0.0;
    vector<Mat> phase_vec, phase_vec2, coh_vec;
    phase_vec.resize(M); phase_vec2.resize(N + 1); coh_vec.resize(M);

    int offset_col, row, count = 0;
    double nearRange, theta = 32.412, spacing, wavelength, B_spatial, B_temporal;
    for (int i = 0; i < M; i++)
    {
        Mat temp;
        conversion.read_int_from_h5(phaseFiles[i].c_str(), "offset_col", &offset_col);
        conversion.read_double_from_h5(phaseFiles[i].c_str(), "slant_range_first_pixel", &nearRange);
        conversion.read_double_from_h5(phaseFiles[i].c_str(), "range_spacing", &spacing);
        conversion.read_double_from_h5(phaseFiles[i].c_str(), "B_spatial", &B_spatial);
        conversion.read_double_from_h5(phaseFiles[i].c_str(), "B_temporal", &B_temporal);
        conversion.read_double_from_h5(phaseFiles[i].c_str(), "carrier_frequency", &wavelength);
        wavelength = VEL_C / wavelength;
        ret = conversion.read_array_from_h5(phaseFiles[i].c_str(), "inc_coefficient", temp);
        if (ret == 0) {
            if (!temp.empty() && temp.type() != CV_64F) temp.convertTo(temp, CV_64F);
            theta = temp.at<double>(0, 0) / 180.0 * PI;
        }
        else
        {
            conversion.read_double_from_h5(phaseFiles[i].c_str(), "inc_center", &theta);
            theta = theta / 180.0 * PI;
        }
        double r = nearRange + double(offset_col) * spacing;
        c.at<double>(i, 0) = 4 * PI / wavelength * B_spatial / sin(theta) / r;
        conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase_vec[i]);
        if (phase_vec[i].type() != CV_64F) phase_vec[i].convertTo(phase_vec[i], CV_64F);
        conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coh_vec[i]);
        if (coh_vec[i].type() != CV_64F) coh_vec[i].convertTo(coh_vec[i], CV_64F);
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
    count = 0;
    
    emit updateProcess(30, QStringLiteral("时间序列分析……"));
#pragma omp parallel for schedule(guided)
    for (int i = 0; i < phase.rows; i++)
    {
        Mat temp(M, 1, CV_64F), temp_coh(M, M, CV_64F); temp = 0.0, temp_coh = 0.0; double coh;
        for (int j = 0; j < phase.cols; j++)
        {
            if (mask.at<int>(i, j) > 0)
            {
                for (int k = 0; k < M; k++)
                {
                    temp.at<double>(k, 0) = phase_vec[k].at<double>(i, j);
                    temp_coh.at<double>(k, k) = coh_vec[k].at<double>(i, j);
                }
                //最小二乘法求解
                Mat A_t, A, b;
                BMc.copyTo(A);
                temp.copyTo(b);
                cv::transpose(A, A_t);
                A = A_t * temp_coh * A;
                b = A_t * temp_coh * b;
                Mat x;
                if (!cv::solve(A, b, x, cv::DECOMP_LU))
                {
                    fprintf(stderr, "SBAS_reference_reselection(): can't solve least square problem!\n");
                }
                else
                {
                    v.at<double>(i, j) = x.at<double>(0, 0);
                    z.at<double>(i, j) = x.at<double>(1, 0);
                }
                //减去地形误差相位
                temp = temp - x.at<double>(1, 0) * c;
                B.copyTo(A);
                temp.copyTo(b);
                cv::transpose(A, A_t);
                A = A_t * temp_coh * A;
                b = A_t * temp_coh * b;
                if (!cv::solve(A, b, x, cv::DECOMP_SVD))
                {
                    fprintf(stderr, "SBAS_reference_reselection(): can't solve SVD!\n");
                }
                else
                {
                    for (int k = 1; k < N + 1; k++)
                    {
                        phase_vec2[k].at<double>(i, j) = x.at<double>(k - 1, 0) *
                            (temporal_baseline.at<double>(0, k) - temporal_baseline.at<double>(0, k - 1)) / 365.0
                            + phase_vec2[k - 1].at<double>(i, j);
                    }
                }
                //计算时间相关系数
                x = B * x;
                coh = 0.0;
                sbas.compute_temporal_coherence(x, temp, &coh);
                temporal_coh.at<double>(i, j) = coh;
            }
        }
    }

    if (QThread::currentThread()->isInterruptionRequested())
    {
        emit errorProcess(QStringLiteral("任务被中断"));
        return;
    }

    //保存时序分析结果
    emit updateProcess(80, QStringLiteral("结果筛选……"));
    int nr, nc;
    nr = mask.rows; nc = mask.cols;
    int valide_count = cv::countNonZero(mask);

    Mat time_series(valide_count, N + 1, CV_64F); time_series = 0.0; valide_count = 0;
    for (int i = 0; i < nr; i++)
    {
        for (int j = 0; j < nc; j++)
        {
            if (mask.at<int>(i, j) == 1)
            {
                Mat series(1, N + 1, CV_64F); Mat temp_A(N + 1, 2, CV_64F); temp_A = 1.0; Mat temp_b(N + 1, 1, CV_64F);
                for (int k = 0; k < N + 1; k++)
                {
                    series.at<double>(0, k) = phase_vec2[k].at<double>(i, j);
                    temp_A.at<double>(k, 0) = temporal_baseline.at<double>(0, k) / 365.0;
                    temp_b.at<double>(k, 0) = phase_vec2[k].at<double>(i, j);
                }
                series.copyTo(time_series(cv::Range(valide_count, valide_count + 1), cv::Range(0, N + 1)));
                valide_count++;
                //最小二乘法拟合线性形变速率
                Mat temp_A_t, temp_x;
                cv::transpose(temp_A, temp_A_t);
                temp_A = temp_A_t * temp_A;
                temp_b = temp_A_t * temp_b;
                if (cv::solve(temp_A, temp_b, temp_x, cv::DECOMP_LU))
                {
                    v.at<double>(i, j) = temp_x.at<double>(0, 0);
                }
            }
        }
    }
    emit updateProcess(95, QStringLiteral("结果保存……"));
    time_series = time_series / 4 / PI * wavelength;
    double max_def, min_def;
    Mat Max(1, 1, CV_64F), Min(1, 1, CV_64F);
    cv::minMaxLoc(time_series, &min_def, &max_def);
    Max.at<double>(0, 0) = max_def;
    Min.at<double>(0, 0) = min_def;
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "max_deformation", Max, 0, 0, 1, 1);
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "min_deformation", Min, 0, 0, 1, 1);
    Mat ref_i(1, 1, CV_32S), ref_j(1, 1, CV_32S);
    ref_i.at<int>(0, 0) = ref_row;
    ref_j.at<int>(0, 0) = ref_col;
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "ref_row", ref_i, 0, 0, 1, 1);
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "ref_col", ref_j, 0, 0, 1, 1);
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "deformation_time_series", time_series, 0, 0, time_series.rows, time_series.cols);
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "temporal_coherence", temporal_coh, 0, 0, temporal_coh.rows, temporal_coh.cols);
    v = v / 4 / PI * wavelength;
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "defomation_velocity", v, 0, 0, v.rows, v.cols);
    conversion.write_subarray_to_h5(times_series_h5_std.c_str(), "residue_topography", z, 0, 0, v.rows, v.cols);

    InSARLogManager::LogInfo("SBASReferenceReselectionWorker", "SBAS Reference Reselection completed successfully.");
    emit endProcess();
}
