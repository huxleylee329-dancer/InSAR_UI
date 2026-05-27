#include "InSARLogManager.h"
#include <memory>
#include "SpeckleDenoiseTask.h"
#include "icon_source.h"
#include "BM3DWrapper.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <opencv2/opencv.hpp>

SpeckleDenoiseTask::SpeckleDenoiseTask(
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
    : m_inputPaths(inputPaths)
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

void SpeckleDenoiseTask::stop()
{
    QMutexLocker locker(&m_lock);
    m_stopFlag = true;
}

void SpeckleDenoiseTask::run()
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
            "SpeckleDenoise", m_inputPaths[i], m_outputPaths[i], m_nodeName,
            m_fileNames.isEmpty() ? QString() : m_fileNames[i],
            m_projectPath, m_projectName, m_model, m_saveToProject, m_projectXml, outError, baseProgress, progressStep
        );

        if (!ok) {
            bool skip = false;
            QString msg = QStringLiteral("处理 %1 时发生错误: %2\n是否跳过并继续处理其余文件？").arg(QFileInfo(m_inputPaths[i]).fileName(), outError);
            emit askUserError(msg, &skip);
            if (!skip) {
                InSARLogManager::LogError("MyThread", QStringLiteral("批处理在 %1 处停止").arg(QFileInfo(m_inputPaths[i]).fileName()));
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

bool SpeckleDenoiseTask::processBM3DEnhancement(
    QString tag,
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

    QString processMsg = (tag == "SpeckleDenoise") ? QStringLiteral("执行BM3D去噪 (可能耗时较长)...") : QStringLiteral("执行BM3D去杂波 (可能耗时较长)...");
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

        QString suffix = (tag == "SpeckleDenoise") ? "_denoised.png" : "_clutter.png";
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

        QStandardItem* projectItem = model->findItems(projectName).isEmpty() ? nullptr : model->findItems(projectName).first();
        if (!projectItem) {
            outError = QStringLiteral("未找到目标工程");
            return false;
        }

        QStandardItem* dataNode = nullptr;
        for (int i = 0; i < projectItem->rowCount(); ++i) {
            if (projectItem->child(i, 0)->text() == nodeName) {
                dataNode = projectItem->child(i, 0);
                break;
            }
        }
        if (!dataNode) {
            dataNode = new QStandardItem(nodeName);
            dataNode->setIcon(QIcon(FOLDER_ICON));
            projectItem->appendRow(dataNode);
        }

        QString defaultDisplay = (tag == "SpeckleDenoise") ? QStringLiteral("denoised") : QStringLiteral("clutter_suppressed");
        QString displayName = fileName.isEmpty() ? defaultDisplay : fileName;
        
        QStandardItem* item_img = NULL;
        for (int j = 0; j < dataNode->rowCount(); j++)
        {
            if (dataNode->child(j, 0)->text() == displayName)
            {
                item_img = dataNode->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* nameItem = new QStandardItem(displayName);
            nameItem->setIcon(QIcon(IMAGEDATA_ICON));
            nameItem->setToolTip(QStringLiteral("image"));
            QStandardItem* pathItem = new QStandardItem(finalPath);
            dataNode->appendRow({ nameItem, pathItem });

            // Update XML for persistence
            {
                QString relativePath = "/" + nodeName + "/" + finalFileName;
                XMLFile* localXml = new XMLFile();
                if (!projectPath.isEmpty()) {
                    localXml->XMLFile_load(projectPath.toStdString().c_str());
                }
                localXml->XMLFile_add_origin(
                    nodeName.toStdString().c_str(),
                    displayName.toStdString().c_str(),
                    relativePath.toStdString().c_str(),
                    tag.toStdString().c_str()
                );
                localXml->XMLFile_save(projectPath.toStdString().c_str());
            }
        }
        else
        {
            dataNode->setChild(item_img->row(), 1, new QStandardItem(finalPath));
        }
    } else {
        cv::imwrite(outputPath.toStdString(), output8U);
    }

    emit updateProcess(100, QStringLiteral("处理完成"));
    return true;
}
