#include "IonosphericCorrectionWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <cmath>
#include <complex>
#include <vector>
#include <AtmosphericCorrection.h>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif

using namespace cv;
using namespace std;

static thread_local IonosphericCorrectionWorker* currentWorker = nullptr;

static bool __stdcall ionosphericProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer s_cbTimer;
    thread_local bool s_timerStarted = false;
    if (!s_timerStarted) {
        s_cbTimer.start();
        s_timerStarted = true;
    }
    if (progress != 0 && progress != 100 && s_cbTimer.elapsed() < 100) {
        return true;
    }
    s_cbTimer.restart();

    if (currentWorker) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }
        emit currentWorker->updateProcess(progress, QString::fromLocal8Bit(message));
    }
    return true;
}

IonosphericCorrectionWorker::IonosphericCorrectionWorker(QObject* parent)
    : BaseWorker(parent)
{
}

IonosphericCorrectionWorker::~IonosphericCorrectionWorker()
{
}

void IonosphericCorrectionWorker::doCorrection(
    double subbandRatio, double filterStrength, bool outputTEC,
    QString save_path, QString project_name,
    QString node_name, QString file_name,
    QStringList slc_names, QStringList slc_paths)
{
    currentWorker = this;
    Q_UNUSED(project_name);
    Q_UNUSED(node_name);
    InSARLogManager::LogInfo("IonosphericCorrectionWorker",
        QString("电离层校正开始. 输出: %1, 子频带比例: %2, 滤波强度: %3")
        .arg(file_name).arg(subbandRatio).arg(filterStrength));

    if (save_path.isEmpty() || project_name.isEmpty() ||
        node_name.isEmpty() || file_name.isEmpty())
    {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    // 输出目录由节点事务预先创建；worker 只允许写入 staging 目录。
    const QString outputDirectoryPath = save_path + "/" + file_name;
    QDir outputDirectory(outputDirectoryPath);
    if (!outputDirectory.exists()) {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("staging输出目录不存在: ") + outputDirectoryPath);
        return;
    }

    // 获取输入文件列表（已配准的主从 SLC 影像对）
    QList<QString> output_names, abs_paths;

    emit updateProcess(5, QStringLiteral("准备数据……"));

    if (slc_names.size() != slc_paths.size()) {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("输入影像快照无效"));
        return;
    }
    for (int i = 0; i < slc_paths.size(); ++i) {
        const QString outName = slc_names[i] + "_iono";
        output_names.append(outName);
        abs_paths.append(outputDirectoryPath + "/" + outName + ".h5");
    }

    int image_count = slc_paths.size();
    if (image_count == 0) {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("没有可处理的SLC影像"));
        return;
    }

    FormatConversion FC;
    int ret = 0;
    const auto fail = [this](const QString& message) {
        currentWorker = nullptr;
        emit errorProcess(message);
    };
    const auto writeMat = [&fail](const QString& filePath, const QString& dataset, const cv::Mat& value) {
        if (NodeUtils::writeMatToH5(filePath, dataset, value)) {
            return true;
        }
        fail(QStringLiteral("无法写入H5数据集: %1").arg(dataset));
        return false;
    };

    std::vector<bool> process_ok(image_count, false);

    // 处理每对 SLC 影像
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) {
            currentWorker = nullptr;
            emit cancelled();
            return;
        }

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("电离层校正第%1/%2幅……").arg(idx + 1).arg(image_count));

        if (idx == 0) {
            // Master 图像作为参考，不进行电离层相位校正，直接拷贝
            {
                NodeUtils::Hdf5Locker locker;
                ret = FC.creat_new_h5(abs_paths[idx].toStdString().c_str());
            }
            if (ret < 0) {
                fail(QStringLiteral("无法创建主影像电离层输出文件"));
                return;
            }

            QString srcH5 = slc_paths[idx];
            QString dstH5 = abs_paths[idx];

            Mat slc_complex;
            {
                NodeUtils::Hdf5Locker locker;
                if (!NodeUtils::readMatFromH5(srcH5, "complex", slc_complex)) {
                    fail(QStringLiteral("无法读取主影像复数数据"));
                    return;
                }
                if (!writeMat(dstH5, QStringLiteral("complex"), slc_complex)) {
                    return;
                }
                if (outputTEC) {
                    const cv::Mat tecPlaceholder = cv::Mat::zeros(slc_complex.rows, slc_complex.cols, CV_32FC1);
                    if (!writeMat(dstH5, QStringLiteral("tec_estimate"), tecPlaceholder)) {
                        return;
                    }
                }

                // 复制元数据
                string tmp_str;
                Mat tmp;
                FormatConversion FC;
                if (NodeUtils::readStringFromH5(srcH5, "source_1", tmp_str))
                    FC.write_str_to_h5(dstH5.toStdString().c_str(), "source_1", tmp_str.c_str());
                if (NodeUtils::readStringFromH5(srcH5, "source_2", tmp_str))
                    FC.write_str_to_h5(dstH5.toStdString().c_str(), "source_2", tmp_str.c_str());
                QString sourcePathMetadataError;
                if (!NodeUtils::copySourcePathMetadata(srcH5, dstH5, &sourcePathMetadataError)) {
                    fail(QStringLiteral("无法保留源路径元数据: %1").arg(sourcePathMetadataError));
                    return;
                }
                if (NodeUtils::readMatFromH5(srcH5, "range_len", tmp))
                    if (!writeMat(dstH5, QStringLiteral("range_len"), tmp)) return;
                if (NodeUtils::readMatFromH5(srcH5, "azimuth_len", tmp))
                    if (!writeMat(dstH5, QStringLiteral("azimuth_len"), tmp)) return;
                if (NodeUtils::readMatFromH5(srcH5, "mapped_lat", tmp))
                    if (!writeMat(dstH5, QStringLiteral("mapped_lat"), tmp)) return;
                if (NodeUtils::readMatFromH5(srcH5, "mapped_lon", tmp))
                    if (!writeMat(dstH5, QStringLiteral("mapped_lon"), tmp)) return;
            }

            process_ok[idx] = true;
            continue;
        }

        // 读取 Master 复数 SLC 数据
        Mat master_complex;
        ret = NodeUtils::readMatFromH5(slc_paths[0], "complex", master_complex) ? 0 : -1;
        if (ret < 0) {
            fail(QStringLiteral("无法读取主影像复数数据"));
            return;
        }

        // 读取 Slave 复数 SLC 数据
        Mat slave_complex;
        ret = NodeUtils::readMatFromH5(slc_paths[idx], "complex", slave_complex) ? 0 : -1;
        if (ret < 0) {
            fail(QStringLiteral("无法读取第%1幅从影像复数数据").arg(idx + 1));
            return;
        }

        int rows = slave_complex.rows;
        int cols = slave_complex.cols;
        if (master_complex.rows != rows || master_complex.cols != cols) {
            InSARLogManager::LogError("IonosphericCorrectionWorker", "主从图像尺寸不匹配");
            fail(QStringLiteral("第%1幅主从影像尺寸不匹配").arg(idx + 1));
            return;
        }

        // 转换为双通道复数格式 (CV_32FC2)
        auto toComplex2Ch = [](const Mat& src, Mat& dst, int r, int c) -> bool {
            if (src.channels() == 1) {
                vector<Mat> channels = {src, Mat::zeros(r, c, CV_32FC1)};
                merge(channels, dst);
            } else if (src.channels() == 2) {
                dst = src.clone();
            } else {
                return false;
            }
            return true;
        };

        Mat master_2ch, slave_2ch;
        if (!toComplex2Ch(master_complex, master_2ch, rows, cols) ||
            !toComplex2Ch(slave_complex, slave_2ch, rows, cols)) {
            fail(QStringLiteral("第%1幅影像复数数据格式无效").arg(idx + 1));
            return;
        }

        // 预分配输出矩阵 (双重保险)
        Mat corrected(rows, cols, CV_32FC2);

        // 设置 DLL 参数
        IonosphericParams params;
        params.subbandRatio = subbandRatio;
        params.filterStrength = filterStrength;

        char errBuf[512] = {0};
        // 调用独立算法 DLL 进行电离层校正
        bool ok = computeIonosphericCorrection(
            master_2ch, slave_2ch, params,
            corrected,
            errBuf, 512,
            ionosphericProgressCallback
        );

        if (!ok) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                currentWorker = nullptr;
                emit cancelled();
                return;
            }
            InSARLogManager::LogError("IonosphericCorrectionWorker", 
                QString("电离层算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            fail(QStringLiteral("电离层算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            return;
        }

        // 写入输出 H5
        {
            NodeUtils::Hdf5Locker locker;
            ret = FC.creat_new_h5(abs_paths[idx].toStdString().c_str());
        }
        if (ret < 0) {
            fail(QStringLiteral("无法创建第%1幅电离层输出文件").arg(idx + 1));
            return;
        }

        // 复制元数据
        {
            NodeUtils::Hdf5Locker locker;
            QString srcH5 = slc_paths[idx];
            QString dstH5 = abs_paths[idx];
            string tmp_str;
            Mat tmp;
            
            NodeUtils::readStringFromH5(srcH5, "source_1", tmp_str);
            FC.write_str_to_h5(dstH5.toStdString().c_str(), "source_1", tmp_str.c_str());
            NodeUtils::readStringFromH5(srcH5, "source_2", tmp_str);
            FC.write_str_to_h5(dstH5.toStdString().c_str(), "source_2", tmp_str.c_str());
            QString sourcePathMetadataError;
            if (!NodeUtils::copySourcePathMetadata(srcH5, dstH5, &sourcePathMetadataError)) {
                fail(QStringLiteral("无法保留源路径元数据: %1").arg(sourcePathMetadataError));
                return;
            }

            if (!NodeUtils::readMatFromH5(srcH5, "range_len", tmp)) {
                fail(QStringLiteral("无法读取第%1幅影像距离尺寸").arg(idx + 1));
                return;
            }
            if (!writeMat(dstH5, QStringLiteral("range_len"), tmp)) return;
            if (!NodeUtils::readMatFromH5(srcH5, "azimuth_len", tmp)) {
                fail(QStringLiteral("无法读取第%1幅影像方位尺寸").arg(idx + 1));
                return;
            }
            if (!writeMat(dstH5, QStringLiteral("azimuth_len"), tmp)) return;

            // 写入校正后的复数 SLC
            if (!writeMat(dstH5, QStringLiteral("complex"), corrected)) return;

            // 可选输出 TEC 估计图
            if (outputTEC) {
                // 由于算法封装至 DLL 内部，此处输出空的 TEC 占位矩阵
                cv::Mat tec_placeholder = cv::Mat::zeros(corrected.rows, corrected.cols, CV_32FC1);
                if (!writeMat(dstH5, QStringLiteral("tec_estimate"), tec_placeholder)) return;
            }

            if (NodeUtils::readMatFromH5(srcH5, "mapped_lat", tmp))
                if (!writeMat(dstH5, QStringLiteral("mapped_lat"), tmp)) return;
            if (NodeUtils::readMatFromH5(srcH5, "mapped_lon", tmp))
                if (!writeMat(dstH5, QStringLiteral("mapped_lon"), tmp)) return;
        }

        process_ok[idx] = true;
    }

    QStringList generatedNames;
    QStringList generatedPaths;
    for (int i = 0; i < image_count; ++i) {
        if (process_ok[i]) {
            generatedNames.append(output_names[i]);
            generatedPaths.append(abs_paths[i]);
        }
    }
    if (generatedPaths.size() != image_count) {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("电离层校正未完成全部输出文件"));
        return;
    }

    currentWorker = nullptr;
    InSARLogManager::LogInfo("IonosphericCorrectionWorker", "电离层校正完成");
    emit outputsGenerated(generatedNames, generatedPaths);
    emit endProcess();
}
