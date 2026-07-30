#include "DeformationRateFieldWorker.h"
#include "DeformationRateField.h"
#include "NodeUtils.h"
#include "FormatConversion.h"
#include "InSARLogManager.h"
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;
using namespace cv;

DeformationRateFieldWorker::DeformationRateFieldWorker(QObject* parent)
    : BaseWorker(parent)
{
}

DeformationRateFieldWorker::~DeformationRateFieldWorker()
{
}

// Draw contours Marching Squares helper
static void drawContoursMarchingSquares(cv::Mat& image, const cv::Mat& velocity, const cv::Mat& mask, double interval) {
    if (interval <= 0) return;
    double min_val = 0.0, max_val = 0.0;
    cv::minMaxLoc(velocity, &min_val, &max_val);
    
    std::vector<double> levels;
    double start_level = std::floor(min_val / interval) * interval;
    for (double lvl = start_level; lvl <= max_val; lvl += interval) {
        if (lvl >= min_val && lvl <= max_val) {
            levels.push_back(lvl);
        }
    }
    
    for (double lvl : levels) {
        for (int r = 0; r < velocity.rows - 1; ++r) {
            for (int c = 0; c < velocity.cols - 1; ++c) {
                if (mask.at<int>(r, c) == 0 || mask.at<int>(r+1, c) == 0 ||
                    mask.at<int>(r, c+1) == 0 || mask.at<int>(r+1, c+1) == 0) {
                    continue;
                }
                
                double v0 = velocity.at<double>(r, c);
                double v1 = velocity.at<double>(r, c+1);
                double v2 = velocity.at<double>(r+1, c+1);
                double v3 = velocity.at<double>(r+1, c);
                
                int index = 0;
                if (v0 >= lvl) index |= 1;
                if (v1 >= lvl) index |= 2;
                if (v2 >= lvl) index |= 4;
                if (v3 >= lvl) index |= 8;
                
                if (index == 0 || index == 15) continue;
                
                auto lerp = [&](double va, double vb, double xa, double xb) {
                    if (std::abs(va - vb) < 1e-9) return 0.5 * (xa + xb);
                    return xa + (lvl - va) / (vb - va) * (xb - xa);
                };
                
                cv::Point2f e0(lerp(v0, v1, c, c+1), r);
                cv::Point2f e1(c+1, lerp(v1, v2, r, r+1));
                cv::Point2f e2(lerp(v3, v2, c, c+1), r+1);
                cv::Point2f e3(c, lerp(v0, v3, r, r+1));
                
                std::vector<std::pair<cv::Point2f, cv::Point2f>> segments;
                switch (index) {
                    case 1: case 14: segments.push_back({e0, e3}); break;
                    case 2: case 13: segments.push_back({e0, e1}); break;
                    case 3: case 12: segments.push_back({e1, e3}); break;
                    case 4: case 11: segments.push_back({e1, e2}); break;
                    case 5:
                        segments.push_back({e0, e1});
                        segments.push_back({e2, e3});
                        break;
                    case 10:
                        segments.push_back({e0, e3});
                        segments.push_back({e1, e2});
                        break;
                    case 6: case 9: segments.push_back({e0, e2}); break;
                    case 7: case 8: segments.push_back({e2, e3}); break;
                }
                
                for (const auto& seg : segments) {
                    cv::line(image, seg.first, seg.second, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
                }
            }
        }
    }
}

void DeformationRateFieldWorker::analyze_rate_field(
    QString  projectPath,
    QString  projectName,
    QString  dstNode,
    QStringList filePaths,
    int      modelType,
    double   confidenceLevel,
    double   coherenceThresholdHigh,
    double   coherenceThresholdMid,
    double   uncertaintyThresholdHigh,
    double   uncertaintyThresholdMid,
    int      colorMap,
    bool     showContour,
    int      contourInterval,
    bool     showArrow,
    int      arrowSpacing,
    bool     outputDirectoryIsStaging
) {
    Q_UNUSED(projectName);
    if (filePaths.isEmpty()) {
        emit errorProcess(QStringLiteral("无输入 SBAS H5 文件！"));
        return;
    }

    QString sbasH5 = filePaths.first();
    QFileInfo h5FileInfo(sbasH5);
    if (!h5FileInfo.exists()) {
        emit errorProcess(QStringLiteral("输入的 SBAS H5 文件不存在：%1").arg(sbasH5));
        return;
    }

    QString projDir = projectPath;
    if (projDir.endsWith(".insar", Qt::CaseInsensitive)) {
        projDir = QFileInfo(projDir).absolutePath();
    }
    QString outDir;
    if (outputDirectoryIsStaging) {
        outDir = projDir;
        if (!QDir(outDir).exists()) {
            emit errorProcess(QStringLiteral("Staging output directory does not exist: %1").arg(outDir));
            return;
        }
    } else {
        outDir = projDir + "/" + dstNode;
        QDir dir(projDir);
        if (!dir.exists(dstNode) && !dir.mkdir(dstNode)) {
            emit errorProcess(QStringLiteral("创建输出目录失败：%1").arg(outDir));
            return;
        }
    }

    QString outH5 = outDir + "/DeformationRateField.h5";
    emit updateProcess(5, QStringLiteral("读取 SBAS 时序数据..."));

    cv::Mat time_series, temporal_baseline, spatial_baseline, mask, temporal_coherence, velocity_linear;
    FormatConversion FC;

    {
        NodeUtils::Hdf5Locker locker;
        
        if (FC.read_array_from_h5(sbasH5.toStdString().c_str(), "deformation_time_series", time_series) != 0) {
            emit errorProcess(QStringLiteral("读取 deformation_time_series 数据集失败！"));
            return;
        }
        if (FC.read_array_from_h5(sbasH5.toStdString().c_str(), "temporal_baseline", temporal_baseline) != 0) {
            emit errorProcess(QStringLiteral("读取 temporal_baseline 数据集失败！"));
            return;
        }
        if (FC.read_array_from_h5(sbasH5.toStdString().c_str(), "spatial_baseline", spatial_baseline) != 0) {
            emit errorProcess(QStringLiteral("读取 spatial_baseline 数据集失败！"));
            return;
        }
        if (FC.read_array_from_h5(sbasH5.toStdString().c_str(), "mask", mask) != 0) {
            emit errorProcess(QStringLiteral("读取 mask 数据集失败！"));
            return;
        }
        if (FC.read_array_from_h5(sbasH5.toStdString().c_str(), "temporal_coherence", temporal_coherence) != 0) {
            emit errorProcess(QStringLiteral("读取 temporal_coherence 数据集失败！"));
            return;
        }
        if (FC.read_array_from_h5(sbasH5.toStdString().c_str(), "defomation_velocity", velocity_linear) != 0) {
            emit errorProcess(QStringLiteral("读取 defomation_velocity 数据集失败！"));
            return;
        }
    }

    if (!temporal_coherence.empty() && temporal_coherence.type() != CV_64F) {
        temporal_coherence.convertTo(temporal_coherence, CV_64F);
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(20, QStringLiteral("执行速率场分析计算..."));

    RateFieldParams params;
    params.model_type = modelType;
    params.confidence_level = confidenceLevel;
    params.estimate_uncertainty = true;
    params.compute_acceleration = (modelType == 2);

    int num_valid = 0;
    for (int r = 0; r < mask.rows; ++r) {
        for (int c = 0; c < mask.cols; ++c) {
            if (mask.at<int>(r, c) > 0) {
                num_valid++;
            }
        }
    }

    if (num_valid == 0) {
        emit errorProcess(QStringLiteral("有效像素数量为 0，无法分析！"));
        return;
    }

    cv::Mat coherence_valid(1, num_valid, CV_64F);
    int valid_idx = 0;
    for (int r = 0; r < mask.rows; ++r) {
        for (int c = 0; c < mask.cols; ++c) {
            if (mask.at<int>(r, c) > 0) {
                coherence_valid.at<double>(0, valid_idx++) = temporal_coherence.at<double>(r, c);
            }
        }
    }

    DeformationRateField drf;
    RateFieldResult result;

    int ret = drf.analyze_rate_field(
        time_series,
        temporal_baseline,
        spatial_baseline,
        mask,
        coherence_valid,
        velocity_linear,
        params,
        result
    );

    if (ret != 0) {
        emit errorProcess(QString::fromStdString(drf.error_head.empty() ? "DLL 速率场计算失败！" : drf.error_head));
        return;
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(50, QStringLiteral("评估速率场质量分级..."));

    cv::Mat velocity_to_assess = (modelType == 2) ? result.velocity_nonlinear : velocity_linear;
    if (velocity_to_assess.type() != CV_64F) {
        velocity_to_assess.convertTo(velocity_to_assess, CV_64F);
    }

    ret = drf.assess_quality(
        velocity_to_assess,
        result.velocity_std,
        temporal_coherence,
        coherenceThresholdHigh,
        coherenceThresholdMid,
        uncertaintyThresholdHigh,
        uncertaintyThresholdMid,
        result.quality_mask,
        result.mean_velocity,
        result.std_velocity,
        result.num_valid_pixels
    );

    if (ret != 0) {
        emit errorProcess(QStringLiteral("DLL 质量评估计算失败！"));
        return;
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(65, QStringLiteral("保存速率分析成果文件..."));

    {
        NodeUtils::Hdf5Locker locker;

        if (FC.creat_new_h5(outH5.toStdString().c_str()) < 0) {
            emit errorProcess(QStringLiteral("创建速率场输出文件失败：%1").arg(outH5));
            return;
        }
        const auto writeFailed = [this, &outH5](int result, const QString& dataset) {
            if (result >= 0) {
                return false;
            }
            emit errorProcess(QStringLiteral("写入速率场数据集失败：%1 (%2)").arg(dataset, outH5));
            return true;
        };

        if (modelType == 2) {
            if (writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "velocity_nonlinear", result.velocity_nonlinear), QStringLiteral("velocity_nonlinear")) ||
                writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "acceleration", result.acceleration), QStringLiteral("acceleration")) ||
                writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "acceleration_std", result.acceleration_std), QStringLiteral("acceleration_std"))) {
                return;
            }
        }
        if (writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "velocity_std", result.velocity_std), QStringLiteral("velocity_std")) ||
            writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "velocity_lower", result.velocity_lower), QStringLiteral("velocity_lower")) ||
            writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "velocity_upper", result.velocity_upper), QStringLiteral("velocity_upper")) ||
            writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "quality_mask", result.quality_mask), QStringLiteral("quality_mask")) ||
            writeFailed(FC.write_array_to_h5(outH5.toStdString().c_str(), "mask", mask), QStringLiteral("mask")) ||
            writeFailed(FC.write_str_to_h5(outH5.toStdString().c_str(), "rate_model_type", (modelType == 2) ? "quadratic" : "linear"), QStringLiteral("rate_model_type")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "confidence_level", confidenceLevel), QStringLiteral("confidence_level")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "coherence_threshold_high", coherenceThresholdHigh), QStringLiteral("coherence_threshold_high")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "coherence_threshold_mid", coherenceThresholdMid), QStringLiteral("coherence_threshold_mid")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "uncertainty_threshold_high", uncertaintyThresholdHigh), QStringLiteral("uncertainty_threshold_high")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "uncertainty_threshold_mid", uncertaintyThresholdMid), QStringLiteral("uncertainty_threshold_mid")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "mean_velocity", result.mean_velocity), QStringLiteral("mean_velocity")) ||
            writeFailed(FC.write_double_to_h5(outH5.toStdString().c_str(), "std_velocity_global", result.std_velocity), QStringLiteral("std_velocity_global")) ||
            writeFailed(FC.write_int_to_h5(outH5.toStdString().c_str(), "num_valid_pixels", result.num_valid_pixels), QStringLiteral("num_valid_pixels")) ||
            writeFailed(FC.write_str_to_h5(outH5.toStdString().c_str(), "analysis_date", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString().c_str()), QStringLiteral("analysis_date")) ||
            writeFailed(FC.write_str_to_h5(outH5.toStdString().c_str(), "sbas_h5_path", sbasH5.toStdString().c_str()), QStringLiteral("sbas_h5_path"))) {
            return;
        }
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    if (outputDirectoryIsStaging) {
        emit updateProcess(100, QStringLiteral("速率场分析完成"));
        emit outputsGenerated(dstNode, outH5);
        emit endProcess();
        return;
    }

    emit updateProcess(80, QStringLiteral("生成可视化渲染图像..."));

    QString overlayJpg = outDir + "/velocity_overlay.jpg";
    QString arrowJpg = outDir + "/velocity_arrow.jpg";
    QString contourJpg = outDir + "/velocity_contour.jpg";
    QString qmaskJpg = outDir + "/quality_mask.jpg";

    cv::Mat velocity_visual = (modelType == 2) ? result.velocity_nonlinear : velocity_linear;
    if (velocity_visual.type() != CV_64F) {
        velocity_visual.convertTo(velocity_visual, CV_64F);
    }

    // 1. Color scale overlay (velocity_overlay.jpg) using 2% percentile stretch
    std::vector<double> valid_vals;
    for (int r = 0; r < velocity_visual.rows; ++r) {
        for (int c = 0; c < velocity_visual.cols; ++c) {
            if (mask.at<int>(r, c) > 0) {
                double val = velocity_visual.at<double>(r, c);
                if (!std::isnan(val) && !std::isinf(val)) {
                    valid_vals.push_back(val);
                }
            }
        }
    }

    double min_stretch = -10.0;
    double max_stretch = 10.0;
    if (!valid_vals.empty()) {
        std::sort(valid_vals.begin(), valid_vals.end());
        int low_idx = static_cast<int>(valid_vals.size() * 0.02);
        int high_idx = static_cast<int>(valid_vals.size() * 0.98);
        if (high_idx >= valid_vals.size()) high_idx = valid_vals.size() - 1;
        min_stretch = valid_vals[low_idx];
        max_stretch = valid_vals[high_idx];
        if (max_stretch <= min_stretch) {
            max_stretch = min_stretch + 1.0;
        }
    }

    cv::Mat normalized = cv::Mat::zeros(velocity_visual.size(), CV_8UC1);
    for (int r = 0; r < velocity_visual.rows; ++r) {
        for (int c = 0; c < velocity_visual.cols; ++c) {
            if (mask.at<int>(r, c) > 0) {
                double val = velocity_visual.at<double>(r, c);
                if (std::isnan(val) || std::isinf(val)) {
                    normalized.at<uchar>(r, c) = 127;
                } else {
                    double norm = (val - min_stretch) / (max_stretch - min_stretch) * 255.0;
                    if (norm < 0.0) norm = 0.0;
                    if (norm > 255.0) norm = 255.0;
                    normalized.at<uchar>(r, c) = static_cast<uchar>(norm);
                }
            } else {
                normalized.at<uchar>(r, c) = 255;
            }
        }
    }

    cv::Mat color_img;
    if (colorMap == 0) {
        cv::Mat lut(1, 256, CV_8UC3);
        for (int i = 0; i < 256; ++i) {
            if (i <= 127) {
                double t = i / 127.0;
                lut.at<cv::Vec3b>(0, i) = cv::Vec3b(255, static_cast<uchar>(255 * t), static_cast<uchar>(255 * t));
            } else {
                double t = (i - 128) / 127.0;
                lut.at<cv::Vec3b>(0, i) = cv::Vec3b(static_cast<uchar>(255 * (1.0 - t)), static_cast<uchar>(255 * (1.0 - t)), 255);
            }
        }
        cv::LUT(normalized, lut, color_img);
    } else if (colorMap == 1) {
        cv::applyColorMap(normalized, color_img, cv::COLORMAP_JET);
    } else {
        cv::applyColorMap(normalized, color_img, cv::COLORMAP_RAINBOW);
    }

    for (int r = 0; r < velocity_visual.rows; ++r) {
        for (int c = 0; c < velocity_visual.cols; ++c) {
            if (mask.at<int>(r, c) == 0) {
                color_img.at<cv::Vec3b>(r, c) = cv::Vec3b(255, 255, 255);
            }
        }
    }
    cv::imwrite(overlayJpg.toStdString(), color_img);

    // 2. Vector Arrow
    if (showArrow) {
        cv::Mat arrow_img = color_img.clone();
        int spacing = arrowSpacing;
        if (spacing < 2) spacing = 10;

        double max_abs_velocity = std::max(std::abs(min_stretch), std::abs(max_stretch));
        if (max_abs_velocity < 1e-3) max_abs_velocity = 1.0;
        double arrow_scale_factor = (spacing * 1.5) / max_abs_velocity;

        for (int r = spacing / 2; r < velocity_visual.rows; r += spacing) {
            for (int c = spacing / 2; c < velocity_visual.cols; c += spacing) {
                if (mask.at<int>(r, c) > 0) {
                    double val = velocity_visual.at<double>(r, c);
                    if (std::isnan(val) || std::isinf(val) || std::abs(val) < 1e-3) continue;

                    double len = std::abs(val) * arrow_scale_factor;
                    if (len > spacing * 3) len = spacing * 3;
                    if (len < 2.0) len = 2.0;

                    cv::Point p_start(c, r);
                    cv::Point p_end;
                    if (val > 0) {
                        p_end = cv::Point(c, static_cast<int>(r - len));
                    } else {
                        p_end = cv::Point(c, static_cast<int>(r + len));
                    }
                    cv::arrowedLine(arrow_img, p_start, p_end, cv::Scalar(0, 0, 0), 1, 8, 0, 0.3);
                }
            }
        }
        cv::imwrite(arrowJpg.toStdString(), arrow_img);
    } else {
        QFile::copy(overlayJpg, arrowJpg);
    }

    // 3. Contour map
    if (showContour) {
        cv::Mat contour_img = color_img.clone();
        drawContoursMarchingSquares(contour_img, velocity_visual, mask, contourInterval);
        cv::imwrite(contourJpg.toStdString(), contour_img);
    } else {
        QFile::copy(overlayJpg, contourJpg);
    }

    // 4. Quality mask preview
    cv::Mat qmask_img(result.quality_mask.size(), CV_8UC3, cv::Scalar(255, 255, 255));
    for (int r = 0; r < result.quality_mask.rows; ++r) {
        for (int c = 0; c < result.quality_mask.cols; ++c) {
            if (mask.at<int>(r, c) > 0) {
                int q = result.quality_mask.at<int>(r, c);
                if (q == 2) {
                    qmask_img.at<cv::Vec3b>(r, c) = cv::Vec3b(0, 180, 0);
                } else if (q == 1) {
                    qmask_img.at<cv::Vec3b>(r, c) = cv::Vec3b(0, 200, 200);
                } else {
                    qmask_img.at<cv::Vec3b>(r, c) = cv::Vec3b(0, 0, 180);
                }
            }
        }
    }
    cv::imwrite(qmaskJpg.toStdString(), qmask_img);

    emit updateProcess(90, QStringLiteral("输出统计分析报告..."));

    // Save CSV
    QString csvPath = outDir + "/statistics_report.csv";
    QFile csvFile(csvPath);
    if (csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&csvFile);
        std::string report_txt;
        drf.generate_statistics_report(result, report_txt);
        ts << QString::fromStdString(report_txt);
        csvFile.close();
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    InSARLogManager::LogInfo("DeformationRateFieldWorker", "Deformation Rate Field Analysis completed successfully.");
    emit outputsGenerated(dstNode, outH5);
    emit endProcess();
}
