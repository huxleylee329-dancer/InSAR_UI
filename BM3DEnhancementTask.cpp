#include "InSARLogManager.h"
#include <memory>
#include "BM3DEnhancementTask.h"
#include "icon_source.h"
#include "BM3DWrapper.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <opencv2/opencv.hpp>

BM3DEnhancementTask::BM3DEnhancementTask(
    EnhancementType type,
    QStringList inputPaths,
    QStringList outputPaths,
    QString nodeName,
    QStringList fileNames,
    QString projectPath,
    QString projectName,
    QStandardItemModel* model,
    bool saveToProject,
    XMLFile* projectXml
)
    : m_type(type)
    , m_inputPaths(inputPaths)
    , m_outputPaths(outputPaths)
    , m_nodeName(nodeName)
    , m_fileNames(fileNames)
    , m_projectPath(projectPath)
    , m_projectName(projectName)
    , m_model(model)
    , m_saveToProject(saveToProject)
    , m_projectXml(projectXml)
    , m_stopFlag(false)
{
}

void BM3DEnhancementTask::stop()
{
    QMutexLocker locker(&m_lock);
    m_stopFlag = true;
}

void BM3DEnhancementTask::run()
{
    for (int i = 0; i < m_inputPaths.size(); ++i) {
        m_lock.lock();
        if (m_stopFlag) {
            m_lock.unlock();
            break;
        }
        m_lock.unlock();

        int baseProgress = i * 100 / m_inputPaths.size();
        int progressStep = 100 / m_inputPaths.size();
        QString outError;
        bool ok = processBM3DEnhancement(
            m_inputPaths[i], m_outputPaths[i], m_nodeName,
            m_fileNames.isEmpty() ? QString() : m_fileNames[i],
            m_projectPath, m_projectName, m_model, m_saveToProject, m_projectXml, outError, baseProgress, progressStep
        );

        if (!ok) {
            bool skip = false;
            QString msg = QStringLiteral("处理 %1 时发生错误: %2\n是否跳过并继续处理其余文件？").arg(QFileInfo(m_inputPaths[i]).fileName(), outError);
            emit askUserError(msg, &skip);
            if (!skip) {
                InSARLogManager::LogError("BM3DEnhancementTask", QStringLiteral("批处理在 %1 处停止").arg(QFileInfo(m_inputPaths[i]).fileName()));
                emit errorProcess(QStringLiteral("批处理在 %1 处停止").arg(QFileInfo(m_inputPaths[i]).fileName()));
                return;
            }
        }

        int overallProgress = (i + 1) * 100 / m_inputPaths.size();
        emit updateProcess(overallProgress, QStringLiteral("批处理进度: %1/%2").arg(i + 1).arg(m_inputPaths.size()));
    }

    emit sendModel(m_model);
    emit endProcess();
}

bool BM3DEnhancementTask::processBM3DEnhancement(
    QString inputPath,
    QString outputPath,
    QString nodeName,
    QString fileName,
    QString projectPath,
    QString projectName,
    QStandardItemModel* model,
    bool saveToProject,
    XMLFile* projectXml,
    QString& outError,
    int baseProgress,
    int progressStep
)
{
    // 根据增强类型动态配置
    QString tag;
    QString processMsg;
    QString suffix;
    QString defaultDisplay;

    if (m_type == EnhancementType::SpeckleDenoise) {
        tag = "SpeckleDenoise";
        processMsg = QStringLiteral("执行BM3D去噪 (可能耗时较长)...");
        suffix = "_denoised.png";
        defaultDisplay = QStringLiteral("denoised");
    } else { // ClutterSuppression
        tag = "ClutterSuppression";
        processMsg = QStringLiteral("执行BM3D去杂波 (可能耗时较长)...");
        suffix = "_clutter.png";
        defaultDisplay = QStringLiteral("clutter_suppressed");
    }

    emit updateProcess(baseProgress + progressStep * 0.0, QStringLiteral("加载图像..."));

    cv::Mat inputGray = cv::imread(inputPath.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty()) {
        outError = QStringLiteral("无法读取输入图像");
        return false;
    }

    emit updateProcess(baseProgress + progressStep * 0.2, QStringLiteral("准备BM3D计算..."));

    const double noiseGain = 1.1;
    cv::Mat imgDouble;
    inputGray.convertTo(imgDouble, CV_64F);
    cv::Mat imgLog;
    cv::log(imgDouble + 1.0, imgLog);

    auto calcMedian = [](const cv::Mat& img) {
        cv::Mat imgCopy = img.clone();
        imgCopy = imgCopy.reshape(0, 1);
        std::sort(imgCopy.begin<double>(), imgCopy.end<double>());
        int n = imgCopy.total();
        if (n % 2 == 0 && n > 0) {
            return (imgCopy.at<double>(n / 2 - 1) + imgCopy.at<double>(n / 2)) / 2.0;
        } else if (n > 0) {
            return imgCopy.at<double>(n / 2);
        }
        return 0.0;
    };

    double minV = 0.0, maxV = 0.0;
    cv::minMaxLoc(imgLog, &minV, &maxV);
    double rangeV = maxV - minV;
    if (rangeV <= 0.0) rangeV = 1.0;
    cv::Mat imgNorm = (imgLog - minV) / rangeV;

    double medianValue = calcMedian(imgLog);
    cv::Mat absDiff;
    cv::absdiff(imgLog, medianValue, absDiff);
    double sigmaEst = calcMedian(absDiff) / 0.6745;
    double sigmaFinal = (sigmaEst * noiseGain) / rangeV;

    emit updateProcess(baseProgress + progressStep * 0.4, processMsg);

    cv::Mat img8U;
    imgNorm.convertTo(img8U, CV_8U, 255.0);
    double sigma8 = sigmaFinal * 255.0;
    cv::Mat den8U = BM3DWrapper::DenoiseGray(img8U, sigma8);

    if (den8U.empty()) {
        outError = QStringLiteral("BM3D处理失败");
        return false;
    }

    emit updateProcess(baseProgress + progressStep * 0.8, QStringLiteral("后处理及保存..."));

    cv::Mat denNorm;
    den8U.convertTo(denNorm, CV_64F, 1.0 / 255.0);
    cv::Mat imgDen = denNorm * rangeV + minV;
    cv::Mat imgOut;
    cv::exp(imgDen, imgOut);
    imgOut = imgOut - 1.0;

    double meanInput = cv::mean(imgDouble)[0];
    double meanOutput = cv::mean(imgOut)[0];
    if (meanOutput != 0.0) {
        imgOut = imgOut * (meanInput / meanOutput);
    }

    cv::min(imgOut, 255.0, imgOut);
    cv::max(imgOut, 0.0, imgOut);

    cv::Mat output8U;
    imgOut.convertTo(output8U, CV_8U);

    if (saveToProject) {
        if (!model) {
            outError = QStringLiteral("Project model is null");
            return false;
        }

        QString projDirStr = projectPath;
        if (projectPath.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projectPath).absolutePath();
        }

        QDir dir(projDirStr);
        if (!dir.exists(nodeName)) {
            dir.mkdir(nodeName);
        }

        QString finalFileName;
        if (fileName.isEmpty()) {
            finalFileName = QFileInfo(inputPath).baseName() + suffix;
        } else {
            if (QFileInfo(fileName).suffix().isEmpty()) {
                finalFileName = fileName + ".png";
            } else {
                finalFileName = fileName;
            }
        }
        QString finalPath = projDirStr + "/" + nodeName + "/" + finalFileName;
        cv::imwrite(finalPath.toStdString(), output8U);

        QString displayName = fileName.isEmpty() ? defaultDisplay : fileName;

        emit saveImageToProjectRequested(projectName, nodeName, displayName, finalPath, tag, finalFileName);
    } else {
        cv::imwrite(outputPath.toStdString(), output8U);
    }

    emit updateProcess(100, QStringLiteral("处理完成"));
    return true;
}
