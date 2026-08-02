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
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFileInfo>
#include <QMessageBox>
#include <QByteArray>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

namespace {
bool __stdcall isCancellationRequested(void* context)
{
    return static_cast<std::atomic_bool*>(context)->load(std::memory_order_relaxed);
}

QString diagnosticString(const char* bytes, int capacity)
{
    const QByteArray value(bytes, capacity);
    const int terminator = value.indexOf('\0');
    return QString::fromUtf8(value.constData(), terminator >= 0 ? terminator : value.size()).trimmed();
}

QString diagnosticStageName(uint32_t stage)
{
    switch (stage) {
    case UNWRAP_DIAGNOSTIC_STAGE_INPUT: return QStringLiteral("input");
    case UNWRAP_DIAGNOSTIC_STAGE_PATH: return QStringLiteral("path");
    case UNWRAP_DIAGNOSTIC_STAGE_PREPARE: return QStringLiteral("prepare");
    case UNWRAP_DIAGNOSTIC_STAGE_LAUNCH: return QStringLiteral("launch");
    case UNWRAP_DIAGNOSTIC_STAGE_JOB: return QStringLiteral("job");
    case UNWRAP_DIAGNOSTIC_STAGE_PROCESS_EXIT: return QStringLiteral("process exit");
    case UNWRAP_DIAGNOSTIC_STAGE_OUTPUT: return QStringLiteral("output validation");
    case UNWRAP_DIAGNOSTIC_STAGE_INTERNAL: return QStringLiteral("internal");
    default: return QStringLiteral("unknown");
    }
}

QString diagnosticFailureMessage(const QString& operation, const UnwrapDiagnostic& diagnostic, int result)
{
    const QString tool = diagnosticString(diagnostic.tool, sizeof(diagnostic.tool));
    const QString summary = diagnosticString(diagnostic.summary, sizeof(diagnostic.summary));
    return QStringLiteral("%1 failed (%2, stage=%3, status=%4, win32Error=%5, exitCode=%6): %7")
        .arg(operation,
             tool.isEmpty() ? QStringLiteral("unwrap") : tool,
             diagnosticStageName(diagnostic.stage))
        .arg(result)
        .arg(diagnostic.win32Error)
        .arg(diagnostic.exitCode)
        .arg(summary.isEmpty() ? QStringLiteral("No diagnostic summary.") : summary);
}
}

SBASTimeSeriesWorker::SBASTimeSeriesWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<SBASTimeSeriesResult>("SBASTimeSeriesResult");
}

SBASTimeSeriesWorker::~SBASTimeSeriesWorker()
{
}

void SBASTimeSeriesWorker::StopProcess()
{
    BaseWorker::StopProcess();
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool SBASTimeSeriesWorker::cancellationRequested() const noexcept
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

void SBASTimeSeriesWorker::SBAS_time_series(double temporal_thresh_low, double temporal_thresh, double spatial_thresh,
                                            int multilook_rg, int multilook_az, int unwrap_method, double alpha,
                                            double coherence_thresh, double temporal_coherence_thresh,
                                            double refinement_coh_thresh, double refinemen_def_thresh,
                                            QString projectPath, QString projectName, QString dstNode, QString csvPath,
                                            QStringList filePaths, bool outputDirectoryIsStaging)
{
    const QString outputDir = QDir(projectPath).absoluteFilePath(dstNode);
    if (!QDir().mkpath(outputDir)) {
        emit errorProcess(QStringLiteral("Unable to create SBAS output directory."));
        return;
    }

    QTemporaryDir workingDirectory;
    if (!workingDirectory.isValid()) {
        emit errorProcess(QStringLiteral("Unable to create temporary SBAS working directory."));
        return;
    }

    QFile csvFile;
    QTemporaryFile stagingCsvFile;
    QIODevice* csvDevice = nullptr;
    if (outputDirectoryIsStaging) {
        if (!stagingCsvFile.open()) {
            emit errorProcess(QStringLiteral("Unable to create temporary SBAS CSV output."));
            return;
        }
        csvDevice = &stagingCsvFile;
    } else {
        const QFileInfo csvInfo(csvPath);
        if (csvPath.isEmpty() || !QDir().mkpath(csvInfo.absolutePath())) {
            emit errorProcess(QStringLiteral("创建csv文件失败，请检查路径是否正确!"));
            return;
        }
        csvFile.setFileName(csvPath);
        if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            emit errorProcess(QStringLiteral("打开csv文件失败，请检查路径是否正确!"));
            return;
        }
        csvDevice = &csvFile;
    }
    QTextStream in(csvDevice);
    const auto cancellationRequested = [this]() { return this->cancellationRequested(); };
    const auto finishCancelled = [this, csvDevice, outputDirectoryIsStaging, &outputDir]() {
        if (csvDevice) csvDevice->close();
        if (outputDirectoryIsStaging) {
            QDir dir(outputDir);
            if (dir.exists() && !dir.removeRecursively()) {
                InSARLogManager::LogWarning("SBASTimeSeriesWorker", "Cancellation cleanup left staging directory: " + outputDir);
            }
        }
        emit cancelled();
    };
    const auto failDiagnostic = [this, &finishCancelled, &cancellationRequested](const QString& operation,
                                                                                  const UnwrapDiagnostic& diagnostic,
                                                                                  int result) {
        if (diagnostic.cancelled != 0 || cancellationRequested()) {
            finishCancelled();
            return;
        }
        const QString stderrTail = diagnosticString(diagnostic.stderrTail, sizeof(diagnostic.stderrTail));
        if (!stderrTail.isEmpty()) {
            InSARLogManager::LogWarning("SBASTimeSeriesWorker", QStringLiteral("%1 stderr: %2").arg(operation, stderrTail));
        }
        emit errorProcess(diagnosticFailureMessage(operation, diagnostic, result));
    };

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

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

    //确定应用程序路径
    string appPath = QCoreApplication::applicationDirPath().toStdString();
    std::replace(appPath.begin(), appPath.end(), '/', '\\');

    /*干涉相位生成*/
    emit updateProcess(10, QStringLiteral("差分干涉相位生成……"));
    Mat temporal, spatial, formation_matrix, spatial_baseline, temporal_baseline;
    util.spatialTemporalBaselineEstimation(SAR_images, 1, temporal, spatial);
    sbas.get_formation_matrix(spatial, temporal, spatial_thresh, temporal_thresh_low, temporal_thresh / 365.0,
        formation_matrix, spatial_baseline, temporal_baseline);
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    QString ifgSavePath = workingDirectory.path();
    string path1 = ifgSavePath.toStdString();
    std::replace(path1.begin(), path1.end(), '/', '\\');
    ret = sbas.generate_interferograms(
        SAR_images, formation_matrix, spatial_baseline, temporal_baseline, multilook_az, multilook_rg,
        path1.c_str(), true, alpha, &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
    if (ret == -2 || cancellationRequested()) {
        finishCancelled();
        return;
    }
    if (ret != 0) {
        emit errorProcess(QStringLiteral("生成干涉图失败"));
        return;
    }

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
        ret = sbas.generate_high_coherence_mask(
            phaseFiles, 3, 3, coherence_thresh, 0.5, mask,
            &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
        if (ret == -2 || cancellationRequested()) {
            finishCancelled();
            return;
        }
        if (ret != 0) {
            emit errorProcess(QStringLiteral("生成高相干掩膜失败"));
            return;
        }
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
            if (cancellationRequested()) {
                InSARLogManager::LogInfo("SBASTimeSeriesWorker", "Task interrupted during unwrap.");
                finishCancelled();
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
                UnwrapDiagnostic diagnostic = {};
                diagnostic.structSize = sizeof(diagnostic);
                ret = unwrap.McfDelaunayEx(mcf_problem.c_str(), appPath.c_str(), nullptr, &diagnostic);
                if (ret != 0) {
                    failDiagnostic(QStringLiteral("SBAS Delaunay MCF"), diagnostic, ret);
                    return;
                }
                sbas.readDIMACS(mcf_solution.c_str(), nodes, edges, triangles, obj);
            }
            ret = sbas.floodFillUnwrap(
                nodes, edges, 1, false,
                &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
            if (ret == -2 || cancellationRequested()) {
                finishCancelled();
                return;
            }
            if (ret != 0) {
                emit errorProcess(QStringLiteral("洪泛解缠失败"));
                return;
            }
            sbas.retrieve_unwrapped_phase(nodes, phase);
            if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
            if (cancellationRequested()) {
                finishCancelled();
                return;
            }
            NodeUtils::Hdf5Locker h5Lock;
            if (conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase) < 0) {
                emit errorProcess(QStringLiteral("Failed to write first-stage unwrapped phase."));
                return;
            }
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
            if (cancellationRequested()) {
                InSARLogManager::LogInfo("SBASTimeSeriesWorker", "Task interrupted during unwrap.");
                finishCancelled();
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
            UnwrapDiagnostic diagnostic = {};
            diagnostic.structSize = sizeof(diagnostic);
            if (unwrap_method == 2)//SNAPHU方法
            {
                ret = unwrap.SnaphuMatrixEx(phase, phase2, path1.c_str(), nullptr, &diagnostic);
            }
            else//MCF方法
            {
                ret = unwrap.MCFEx(phase, phase2, coherence, residue, mcf_problem.c_str(), appPath.c_str(),
                                   nullptr, &diagnostic);
            }
            if (ret != 0) {
                failDiagnostic(unwrap_method == 2 ? QStringLiteral("SBAS SNAPHU") : QStringLiteral("SBAS MCF"),
                               diagnostic, ret);
                return;
            }
            if (cancellationRequested()) {
                finishCancelled();
                return;
            }
            if (phase2.type() != CV_32F) phase2.convertTo(phase2, CV_32F);
            NodeUtils::Hdf5Locker h5Lock;
            if (conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase2) < 0) {
                emit errorProcess(QStringLiteral("Failed to write first-stage unwrapped phase."));
                return;
            }
            int process = double(i + 1) / phaseFiles.size() * 100.0 * 0.5;
            emit updateProcess(10 + process, QStringLiteral("相位解缠中……"));
        }
    }

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

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
        ret = sbas.refinement_and_reflattening(
            phase, mask, coherence, refinement_coh_thresh,
            &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
        if (ret == -2 || cancellationRequested()) {
            finishCancelled();
            return;
        }
        if (ret != 0) {
            emit errorProcess(QStringLiteral("轨道精炼和重去平失败"));
            return;
        }
        phase = phase - phase.at<double>(ref_i, ref_j);
        if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
        if (cancellationRequested()) {
            finishCancelled();
            return;
        }
        NodeUtils::Hdf5Locker h5Lock;
        if (conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase) < 0) {
            emit errorProcess(QStringLiteral("Failed to write refined unwrapped phase."));
            return;
        }
    }

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

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
        if (cancel_flag || cancellationRequested()) {
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

    if (cancel_flag || cancellationRequested()) {
        finishCancelled();
        return;
    }

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
            ret = sbas.refinement_and_reflattening(
                phase, refinement_mask, coherence, refinement_coh_thresh,
                &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
            if (ret == -2 || cancellationRequested()) {
                finishCancelled();
                return;
            }
            if (ret != 0) {
                emit errorProcess(QStringLiteral("轨道精炼和重去平失败"));
                return;
            }
            phase = phase - phase.at<double>(ref_i, ref_j);
            phase.copyTo(phase_vec[i]);
            if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
            if (cancellationRequested()) {
                finishCancelled();
                return;
            }
            NodeUtils::Hdf5Locker h5Lock;
            if (conversion.write_subarray_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase, 0, 0,
                                                phase.rows, phase.cols) < 0) {
                emit errorProcess(QStringLiteral("Failed to write second-stage refined phase."));
                return;
            }
        }
    }

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(75, QStringLiteral("时间序列分析(2/2)……"));
    std::atomic<int> completed_rows2(0);
    std::atomic<bool> cancel_flag2(false);
    std::atomic<int> max_reported_pct2(75);
    int step_val2 = std::max(1, phase.rows / 10);

#pragma omp parallel for schedule(guided)
    for (int i = 0; i < phase.rows; i++)
    {
        if (cancel_flag2 || cancellationRequested()) {
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

    if (cancel_flag2 || cancellationRequested()) {
        finishCancelled();
        return;
    }

    //保存时序分析结果
    emit updateProcess(80, QStringLiteral("结果筛选……"));
    Mat out_mask, mask_count_map;
    mask.copyTo(out_mask);
    mask.copyTo(mask_count_map);
    out_mask = 0; mask_count_map = 0;
    string times_series_h5 = QDir::toNativeSeparators(
        QDir(outputDir).absoluteFilePath(QStringLiteral("SBAS_time_series.h5"))).toStdString();
    {
        NodeUtils::Hdf5Locker h5Lock;
        if (conversion.creat_new_h5(times_series_h5.c_str()) < 0) {
            emit errorProcess(QStringLiteral("Failed to create SBAS time-series H5 output."));
            return;
        }
    }
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    int nr, nc;
    nr = mask.rows; nc = mask.cols;
    int valide_count = 0;
    for (int i = 0; i < nr; i++)
    {
        if ((i & 31) == 0 && cancellationRequested()) {
            finishCancelled();
            return;
        }
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
            if ((i & 31) == 0 && cancellationRequested()) {
                finishCancelled();
                return;
            }
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
    csvDevice->close();
    
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(95, QStringLiteral("结果保存……"));
    Mat mapped_lat, mapped_lon;
    double max_def, min_def;
    
    const auto writeOrFail = [this](int status, const QString& dataset) {
        if (status >= 0) return true;
        emit errorProcess(QStringLiteral("Failed to write SBAS output dataset: %1").arg(dataset));
        return false;
    };

    NodeUtils::Hdf5Locker h5Lock;
    if (!writeOrFail(conversion.write_int_to_h5(times_series_h5.c_str(), "ref_row", ref_i), QStringLiteral("ref_row"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_int_to_h5(times_series_h5.c_str(), "ref_col", ref_j), QStringLiteral("ref_col"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_int_to_h5(times_series_h5.c_str(), "multilook_rg", multilook_rg), QStringLiteral("multilook_rg"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_int_to_h5(times_series_h5.c_str(), "multilook_az", multilook_az), QStringLiteral("multilook_az"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "temporal_baseline", temporal), QStringLiteral("temporal_baseline"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "spatial_baseline", spatial), QStringLiteral("spatial_baseline"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "formation_matrix", formation_matrix), QStringLiteral("formation_matrix"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "mask", out_mask), QStringLiteral("mask"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "mask_count_map", mask_count_map), QStringLiteral("mask_count_map"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    time_series = time_series / 4 / PI * wavelength;
    cv::minMaxLoc(time_series, &min_def, &max_def);
    if (!writeOrFail(conversion.write_double_to_h5(times_series_h5.c_str(), "max_deformation", max_def), QStringLiteral("max_deformation"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_double_to_h5(times_series_h5.c_str(), "min_deformation", min_def), QStringLiteral("min_deformation"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "deformation_time_series", time_series), QStringLiteral("deformation_time_series"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "temporal_coherence", temporal_coh), QStringLiteral("temporal_coherence"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    v = v / 4 / PI * wavelength;
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "defomation_velocity", v), QStringLiteral("defomation_velocity"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
    if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "residue_topography", z), QStringLiteral("residue_topography"))) return;
    if (cancellationRequested()) { finishCancelled(); return; }
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
            if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "mapped_lat", lat_new), QStringLiteral("mapped_lat"))) return;
            if (cancellationRequested()) { finishCancelled(); return; }
            if (!writeOrFail(conversion.write_array_to_h5(times_series_h5.c_str(), "mapped_lon", lon_new), QStringLiteral("mapped_lon"))) return;
            if (cancellationRequested()) { finishCancelled(); return; }
            break;
        }
    }
    
    QString times_series_h5_forward = QString::fromStdString(times_series_h5).replace('\\', '/');
    QString relativePath = QString("/%1/SBAS_time_series.h5").arg(dstNode);

    SBASTimeSeriesResult sbasRes;
    sbasRes.dstNode = dstNode;
    sbasRes.timesSeriesH5Path = times_series_h5_forward;
    sbasRes.relativePath = relativePath;
    Q_EMIT sbasGenerated(sbasRes);

    InSARLogManager::LogInfo("SBASTimeSeriesWorker", "SBAS Time Series analysis completed successfully.");
    emit endProcess();
}
