#include "S1DeburstWorker.h"
#include "FormatConversion.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QList>
#include <QIcon>
#include <vector>
#include <string>
#include <QStringList>
#include <memory>

S1DeburstWorker::S1DeburstWorker(QObject* parent)
    : BaseWorker(parent)
{
}

S1DeburstWorker::~S1DeburstWorker()
{
}

void S1DeburstWorker::S1_Deburst(
    QString savePath, 
    QString dstProject, 
    QString dstNode, 
    QStringList inputPaths
)
{
    InSARLogManager::LogInfo("S1DeburstWorker", QString("S1_Deburst started. Project: %1, Save Path: %2").arg(dstProject).arg(savePath));
    if (savePath.isEmpty() || dstProject.isEmpty() || dstNode.isEmpty() || inputPaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("参数错误"));
        return;
    }
    int ret;
    std::vector<std::string> SAR_images;
    std::vector<std::string> SAR_images_deburst;
    QList<QString> origin;
    for (const QString& inputPath : inputPaths) {
        const QString originName = QFileInfo(inputPath).baseName();
        origin.append(originName);
        SAR_images.push_back(inputPath.toStdString());
        SAR_images_deburst.push_back(QString("%1/%2/%3_deburst.h5").arg(savePath).arg(dstNode)
            .arg(originName).toStdString());
    }
    QDir dir(savePath);
    if (!dir.exists(dstNode) && !dir.mkdir(dstNode)) {
        emit errorProcess(QStringLiteral("无法创建输出目录"));
        return;
    }
    
    if (SAR_images.empty())
    {
        emit errorProcess(QStringLiteral("无可处理数据"));
        return;
    }

    const auto finishCancelled = [this, &SAR_images_deburst]() {
        for (const std::string& outputPath : SAR_images_deburst) {
            const QString h5Path = QString::fromStdString(outputPath);
            const QFileInfo fileInfo(h5Path);
            QFile::remove(h5Path);
            QFile::remove(fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".jpg");
        }
        emit cancelled();
    };

    std::unique_ptr<NodeUtils::Hdf5Locker> locker(new NodeUtils::Hdf5Locker());
    emit updateProcess(10, QStringLiteral("开始burst拼接……"));
    InSARLogManager::LogInfo("S1DeburstWorker", QString("Starting burst splicing. Total images: %1, Destination Node: %2")
        .arg(SAR_images.size()).arg(dstNode));
    // burst拼接
    for (size_t i = 1; i <= SAR_images.size(); i++)
    {
        if (QThread::currentThread()->isInterruptionRequested())
        {
            finishCancelled();
            return;
        }
        InSARLogManager::LogInfo("S1DeburstWorker", QString("Processing image %1/%2: %3")
            .arg(i).arg(SAR_images.size()).arg(origin[i - 1]));
        Sentinel1Utils su(SAR_images[i - 1].c_str());
        ret = su.init();
        if (QThread::currentThread()->isInterruptionRequested()) {
            finishCancelled();
            return;
        }
        if (ret < 0) {
            emit errorProcess(QStringLiteral("Sentinel1Utils 初始化失败"));
            return;
        }
        ret = su.deburst(SAR_images_deburst[i - 1].c_str());
        if (QThread::currentThread()->isInterruptionRequested()) {
            finishCancelled();
            return;
        }
        if (ret < 0) {
            emit errorProcess(QStringLiteral("deburst 拼接失败"));
            return;
        }
        InSARLogManager::LogInfo("S1DeburstWorker", QString("Debursting image %1/%2 finished successfully.").arg(i).arg(SAR_images.size()));
        
        // 拼接成功后，立刻生成 JPG 预览图（在后台线程中执行）
        QString h5Path = QString::fromStdString(SAR_images_deburst[i - 1]);
        QFileInfo fi(h5Path);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "complex");
        if (QThread::currentThread()->isInterruptionRequested()) {
            finishCancelled();
            return;
        }

        emit updateProcess(int(10.0 + 80.0 / SAR_images.size() * i), QStringLiteral("burst拼接进度%1%").arg(int(10.0 + 80.0 / SAR_images.size() * i)));
    }
    
    if (QThread::currentThread()->isInterruptionRequested())
    {
        finishCancelled();
        return;
    }

    locker.reset();

    /*建立deburst根节点*/
    QStringList deburstH5Paths;
    QStringList originNames;

    /*建立deburst根节点*/
    for (size_t i = 0; i < SAR_images_deburst.size(); ++i) {
        deburstH5Paths.append(QString::fromStdString(SAR_images_deburst.at(i)));
        originNames.append(origin.at(static_cast<int>(i)));
    }
    // 回传 H5 路径列表和 origin 名称列表，由 Node 端用原生 TinyXML 写入 XML
    emit sendResults(dstNode, deburstH5Paths, originNames);
    InSARLogManager::LogInfo("S1DeburstWorker", "Model and result lists successfully emitted to Node UI.");
    InSARLogManager::LogInfo("S1DeburstWorker", "S1_Deburst finished successfully.");
    emit endProcess();
}
