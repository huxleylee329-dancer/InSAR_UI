#include "InSARLogManager.h"
#include "GenericSARImportTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

bool isSupportedGenericSarRasterPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg") ||
           suffix == QStringLiteral("png") || suffix == QStringLiteral("bmp") ||
           suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff");
}

GenericSARImportTask::GenericSARImportTask(QString xmlFilename,
                                           QString projectPath,
                                           QString folder,
                                           QString filename,
                                           std::shared_ptr<std::atomic_bool> cancellationToken)
    : m_xmlFilename(xmlFilename)
    , m_projectPath(projectPath)
    , m_folder(folder)
    , m_filename(filename)
    , m_cancellationToken(cancellationToken ? cancellationToken
                                            : std::make_shared<std::atomic_bool>(false))
{
    setAutoDelete(false);
}

GenericSARImportTask::~GenericSARImportTask()
{
}

void GenericSARImportTask::stop()
{
    m_cancellationToken->store(true, std::memory_order_relaxed);
}

void GenericSARImportTask::run()
{
    if (m_xmlFilename.isEmpty() || m_projectPath.isEmpty() ||
        m_folder.isEmpty() || m_filename.isEmpty() ||
        !isSupportedGenericSarRasterPath(m_xmlFilename)) {
        emit errorProcess(QStringLiteral("Invalid generic SAR import parameters."));
        return;
    }

    if (m_cancellationToken->load(std::memory_order_relaxed)) {
        emit cancelled();
        return;
    }

    QDir projectDir(m_projectPath);
    if (!projectDir.exists(m_folder) && !projectDir.mkdir(m_folder)) {
        emit errorProcess(QStringLiteral("Failed to create generic SAR output directory."));
        return;
    }

    emit updateProcess(20, QStringLiteral("Importing data..."));

    const QString suffix = QFileInfo(m_xmlFilename).suffix();
    const QString imagePath = QString("%1/%2/%3.%4")
        .arg(m_projectPath, m_folder, m_filename, suffix);

    if (QFile::exists(imagePath)) {
        QFile::remove(imagePath);
    }

    if (!QFile::copy(m_xmlFilename, imagePath)) {
        QFile::remove(imagePath);
        QDir(m_projectPath + "/" + m_folder).removeRecursively();
        emit errorProcess(QStringLiteral("Failed to copy generic SAR input."));
        return;
    }
    if (m_cancellationToken->load(std::memory_order_relaxed)) {
        QFile::remove(imagePath);
        QDir(m_projectPath + "/" + m_folder).removeRecursively();
        emit cancelled();
        return;
    }

    emit updateProcess(90, QStringLiteral("Finishing import..."));
    if (m_cancellationToken->load(std::memory_order_relaxed)) {
        QFile::remove(imagePath);
        QDir(m_projectPath + "/" + m_folder).removeRecursively();
        emit cancelled();
        return;
    }
    emit outputsGenerated(m_folder, {m_filename}, {imagePath}, "complex", "Generic_SAR");
    emit endProcess();
}

GenericSARBatchImportTask::GenericSARBatchImportTask(
    QString savepath,
    std::vector<QString> originalFileList,
    std::vector<QString> importNamelist,
    QString dstNode,
    std::shared_ptr<std::atomic_bool> cancellationToken)
    : m_savepath(savepath)
    , m_originalFileList(originalFileList)
    , m_importNamelist(importNamelist)
    , m_dstNode(dstNode)
    , m_cancellationToken(cancellationToken ? cancellationToken
                                            : std::make_shared<std::atomic_bool>(false))
{
    setAutoDelete(false);
}

GenericSARBatchImportTask::~GenericSARBatchImportTask()
{
}

void GenericSARBatchImportTask::stop()
{
    m_cancellationToken->store(true, std::memory_order_relaxed);
}

void GenericSARBatchImportTask::run()
{
    if (m_savepath.isEmpty() || m_dstNode.isEmpty() || m_originalFileList.empty() ||
        m_importNamelist.empty() || m_originalFileList.size() != m_importNamelist.size()) {
        emit errorProcess(QStringLiteral("Invalid generic SAR batch import parameters."));
        return;
    }

    for (const QString& sourcePath : m_originalFileList) {
        if (!isSupportedGenericSarRasterPath(sourcePath)) {
            emit errorProcess(QStringLiteral("Generic SAR import accepts only raster image files."));
            return;
        }
    }

    if (m_cancellationToken->load(std::memory_order_relaxed)) {
        emit cancelled();
        return;
    }

    QDir projectDir(m_savepath);
    if (!projectDir.exists(m_dstNode) && !projectDir.mkdir(m_dstNode)) {
        emit errorProcess(QStringLiteral("Failed to create generic SAR output directory."));
        return;
    }

    const int imageCount = static_cast<int>(m_originalFileList.size());
    QStringList outputNames;
    QStringList outputPaths;
    emit updateProcess(2, QStringLiteral("Importing data..."));

    for (int i = 0; i < imageCount; ++i) {
        if (m_cancellationToken->load(std::memory_order_relaxed)) {
            QDir(m_savepath + "/" + m_dstNode).removeRecursively();
            emit cancelled();
            return;
        }

        const QString suffix = QFileInfo(m_originalFileList[i]).suffix();
        const QString imagePath = QString("%1/%2/%3.%4")
            .arg(m_savepath, m_dstNode, m_importNamelist[i], suffix);

        if (QFile::exists(imagePath)) {
            QFile::remove(imagePath);
        }

        if (!QFile::copy(m_originalFileList[i], imagePath)) {
            QFile::remove(imagePath);
            QDir(m_savepath + "/" + m_dstNode).removeRecursively();
            InSARLogManager::LogError("GenericSARBatchImportTask", "Failed to copy input file.");
            emit errorProcess(QStringLiteral("Failed to copy generic SAR input."));
            return;
        }

        outputNames.append(m_importNamelist[i]);
        outputPaths.append(imagePath);
        emit updateProcess((i + 1) * 100 / imageCount, QStringLiteral("Importing data..."));
    }

    if (m_cancellationToken->load(std::memory_order_relaxed)) {
        QDir(m_savepath + "/" + m_dstNode).removeRecursively();
        emit cancelled();
        return;
    }

    emit outputsGenerated(m_dstNode, outputNames, outputPaths, "complex", "Generic_SAR");
    emit endProcess();
}
