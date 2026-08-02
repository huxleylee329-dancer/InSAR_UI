#include "SBASReferenceReselectionWorker.h"
#include "SBASTimeSeriesWorker.h"
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
#include <QTemporaryDir>
#include <vector>
#include <string>
#include <algorithm>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

namespace {
bool __stdcall isCancellationRequested(void* context)
{
    return static_cast<std::atomic_bool*>(context)->load(std::memory_order_relaxed);
}
}

SBASReferenceReselectionWorker::SBASReferenceReselectionWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<SBASReferenceReselectionResult>("SBASReferenceReselectionResult");
}

SBASReferenceReselectionWorker::~SBASReferenceReselectionWorker()
{
}

void SBASReferenceReselectionWorker::StopProcess()
{
    BaseWorker::StopProcess();
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool SBASReferenceReselectionWorker::cancellationRequested() const noexcept
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

void SBASReferenceReselectionWorker::SBAS_reference_reselection(QString projectRoot, QString stagingNode,
                                                                QStringList sourceInputs, SBASRebuildParameters parameters,
                                                                int ref_row, int ref_col, QList<QPoint> GCPs)
{
    Utils util; SBAS sbas; FormatConversion conversion;
    int ret;
    const auto finishCancelled = [this]() { emit cancelled(); };

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    
    QTemporaryDir reconstructedInterferograms;
    if (!reconstructedInterferograms.isValid()) {
        emit errorProcess(QStringLiteral("Unable to create temporary SBAS reconstruction directory."));
        return;
    }

    bool reconstructionCancelled = false;
    QString reconstructionError;
    SBASTimeSeriesWorker reconstructor;
    QObject::connect(&reconstructor, &SBASTimeSeriesWorker::cancelled, [&reconstructionCancelled]() {
        reconstructionCancelled = true;
    });
    QObject::connect(&reconstructor, &SBASTimeSeriesWorker::errorProcess, [&reconstructionError](const QString& error) {
        reconstructionError = error;
    });
    emit updateProcess(2, QStringLiteral("正在根据 SBAS provenance 重建干涉图..."));
    reconstructor.SBAS_time_series(parameters.temporalThreshLow, parameters.temporalThresh,
                                   parameters.spatialThresh, parameters.multilookRg, parameters.multilookAz,
                                   parameters.unwrapMethod, parameters.alpha, parameters.coherenceThresh,
                                   parameters.temporalCoherenceThresh, parameters.refinementCohThresh,
                                   parameters.refinementDefThresh, projectRoot, QString(), stagingNode, QString(),
                                   sourceInputs, true, reconstructedInterferograms.path(), &m_cancelRequested);
    if (reconstructionCancelled || cancellationRequested()) {
        finishCancelled();
        return;
    }
    if (!reconstructionError.isEmpty()) {
        emit errorProcess(QStringLiteral("Unable to rebuild SBAS interferograms: %1").arg(reconstructionError));
        return;
    }

    const QString times_series_h5 = QDir(projectRoot).absoluteFilePath(
        stagingNode + QStringLiteral("/SBAS_time_series.h5"));
    const string times_series_h5_std = QDir::toNativeSeparators(times_series_h5).toStdString();
    Mat formation_matrix, mask, temporal_baseline, reflattening_mask;
    ret = NodeUtils::readMatFromH5(times_series_h5, "formation_matrix", formation_matrix) ? 0 : -1;
    if (ret == 0) {
        ret = NodeUtils::readMatFromH5(times_series_h5, "mask", mask) ? 0 : -1;
    }
    if (ret == 0) {
        ret = NodeUtils::readMatFromH5(times_series_h5, "temporal_baseline", temporal_baseline, CV_64F) ? 0 : -1;
    }
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    if (ret != 0) {
        emit errorProcess(QStringLiteral("读取 SBAS 时序输入失败"));
        return;
    }
    
    const QString ifgSavePath = reconstructedInterferograms.path();

    int num_GCPs = GCPs.size();
    mask.copyTo(reflattening_mask); reflattening_mask = 0;
    for (int i = 0; i < num_GCPs; i++)
    {
        reflattening_mask.at<int>(GCPs[i].x(), GCPs[i].y()) = 1;
    }

    vector<string> phaseFiles;
    int n_images = formation_matrix.rows;
    for (int i = 0; i < n_images; i++)
    {
        for (int j = 0; j < i; j++)
        {
            if (formation_matrix.at<int>(i, j) == 1)
            {
                phaseFiles.push_back(QDir::toNativeSeparators(
                    QDir(ifgSavePath).absoluteFilePath(QStringLiteral("%1_%2.h5").arg(i + 1).arg(j + 1))).toStdString());
            }
        }
    }
    
    Mat coherence, phase;
    /*轨道精炼和重去平*/
    emit updateProcess(10, QStringLiteral("轨道精炼和重去平……"));
    for (int i = 0; i < phaseFiles.size(); i++)
    {
        if (cancellationRequested())
        {
            finishCancelled();
            return;
        }
        QString pFile = QString::fromStdString(phaseFiles[i]);
        NodeUtils::readMatFromH5(pFile, "unwrapped_phase_1", phase, CV_64F);
        NodeUtils::readMatFromH5(pFile, "coherence", coherence, CV_64F);
        ret = sbas.refinement_and_reflattening(
            phase, reflattening_mask, coherence, 0.0,
            &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
        if (ret == -2 || cancellationRequested()) {
            finishCancelled();
            return;
        }
        if (ret != 0) {
            emit errorProcess(QStringLiteral("轨道精炼和重去平失败"));
            return;
        }
        phase = phase - phase.at<double>(ref_row, ref_col);
        if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
        if (cancellationRequested()) {
            finishCancelled();
            return;
        }
        NodeUtils::Hdf5Locker writeLock(pFile);
        if (!writeLock.isLocked() ||
            conversion.write_subarray_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase, 0, 0,
                                             phase.rows, phase.cols) != 0) {
            emit errorProcess(QStringLiteral("Failed to write reflattened interferogram phase."));
            return;
        }
        if (cancellationRequested()) {
            finishCancelled();
            return;
        }
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
        if (cancellationRequested()) {
            finishCancelled();
            return;
        }
        Mat temp;
        QString pFile = QString::fromStdString(phaseFiles[i]);
        NodeUtils::readScalarFromH5(pFile, "offset_col", offset_col);
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
        double r = nearRange + double(offset_col) * spacing;
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
    count = 0;
    
    emit updateProcess(30, QStringLiteral("时间序列分析……"));
#pragma omp parallel for schedule(guided)
    for (int i = 0; i < phase.rows; i++)
    {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            continue;
        }
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

    if (cancellationRequested())
    {
        finishCancelled();
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
        if ((i & 31) == 0 && cancellationRequested()) {
            finishCancelled();
            return;
        }
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
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    NodeUtils::Hdf5Locker outputWriteLock(times_series_h5);
    if (!outputWriteLock.isLocked()) {
        emit errorProcess(QStringLiteral("Failed to lock staged SBAS output for reference reselection."));
        return;
    }
    const auto writeOrFail = [this, &conversion, &times_series_h5_std](const char* dataset, Mat& value,
                                                                         int rows, int cols) {
        if (conversion.write_subarray_to_h5(times_series_h5_std.c_str(), dataset, value, 0, 0, rows, cols) == 0) {
            return true;
        }
        emit errorProcess(QStringLiteral("Failed to write SBAS reselection output dataset: %1").arg(QString::fromLatin1(dataset)));
        return false;
    };
    if (!writeOrFail("max_deformation", Max, 1, 1)) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("min_deformation", Min, 1, 1)) return;
    Mat ref_i(1, 1, CV_32S), ref_j(1, 1, CV_32S);
    ref_i.at<int>(0, 0) = ref_row;
    ref_j.at<int>(0, 0) = ref_col;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("ref_row", ref_i, 1, 1)) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("ref_col", ref_j, 1, 1)) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("deformation_time_series", time_series, time_series.rows, time_series.cols)) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("temporal_coherence", temporal_coh, temporal_coh.rows, temporal_coh.cols)) return;
    v = v / 4 / PI * wavelength;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("defomation_velocity", v, v.rows, v.cols)) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail("residue_topography", z, v.rows, v.cols)) return;

    SBASReferenceReselectionResult result;
    result.outputH5Path = times_series_h5;
    emit reselectionGenerated(result);
    InSARLogManager::LogInfo("SBASReferenceReselectionWorker", "SBAS Reference Reselection completed successfully.");
    emit endProcess();
}
