#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "BaseImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "ImportOutputPersistence.h"

#include <QApplication>
#include <QThread>
#include <QFileInfo>
#include <QSet>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>


namespace QtNodes {

ImportNodeBase::ImportNodeBase()
    : ExecutableNodeDelegateModel()
    , m_stopRequested(false)
{
    // Set default execution mode to Manual for import nodes
    // These nodes require user selection and shouldn't trigger the workflow automatically
    setExecutionMode(ExecutionMode::Manual);
}

ImportNodeBase::~ImportNodeBase()
{
    // 安全断开并取消 remedyWatcher，避免悬空回调
    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
    }

    // 统一清理工作线程
    if (m_worker) {
        if (m_thread && m_thread->isRunning()) {
            m_worker->StopProcess();
        }
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    if (m_thread) {
        if (m_thread->isRunning()) {
            m_thread->quit();
            m_thread->wait();
        }
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

// ============================================================================
// 统一的端口配置（默认 2 端口：Port0=成果, Port1=预览）
// ============================================================================

unsigned int ImportNodeBase::nPorts(PortType portType) const
{
    return (portType == PortType::In) ? 0 : 2;
}

NodeDataType ImportNodeBase::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool ImportNodeBase::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString ImportNodeBase::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool ImportNodeBase::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

std::shared_ptr<NodeData> ImportNodeBase::outData(PortIndex port)
{
    // 状态守卫：非 Completed 状态时返回 nullptr，确保脏传播能正确级联 Idle 到下游
    // 否则 setState(Running) 清空 _outputData 后，fallback 到成员变量仍返回旧数据，
    // 导致下游 setInData 收不到 nullptr，无法触发 setState(Idle) 级联
    if (executionState() != ExecutionState::Completed)
        return nullptr;

    // First try to get data from ExecutableNodeDelegateModel base class
    auto data = ExecutableNodeDelegateModel::outData(port);
    if (data)
        return data;

    // Fall back to member variables（兼容子类忘记调用 setOutputData 的情况）
    if (port == 0) return m_importedFiles;
    if (port == 1) return m_imageInfo;

    return nullptr;
}

QStringList ImportNodeBase::previewImagePaths() const
{
    QStringList jpgPaths;
    for (const QString& h5Path : m_importedFilePaths) {
        QFileInfo fi(h5Path);
        QString jpg = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        if (QFileInfo::exists(jpg)) {
            jpgPaths.append(jpg);
        }
    }
    return jpgPaths;
}

QStringList ImportNodeBase::getExpectedPreviewFilePaths() const
{
    QStringList previewPaths;
    const QStringList outputPaths = getExpectedOutputFilePaths();
    for (const QString& outputPath : outputPaths) {
        const QFileInfo info(outputPath);
        previewPaths.append(info.absolutePath() + QStringLiteral("/") +
                            info.baseName() + QStringLiteral(".jpg"));
    }
    return previewPaths;
}

QStringList ImportNodeBase::transactionInputPaths() const
{
    return QStringList();
}

void ImportNodeBase::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    // Call base class implementation
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* ImportNodeBase::embeddedWidget()
{
    // Create widget on first access
    if (!_widget)
    {
        _widget = createWidget();

        // Set object name for QSS targeting
        _widget->setObjectName("NodeEmbeddedWidget");
    }
    return _widget;
}

QStandardItemModel* ImportNodeBase::projectModel() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectModel() : nullptr;
}

QString ImportNodeBase::projectPath() const
{
    auto iface = getProjectContext();
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.isEmpty()) return QString();
        return QFileInfo(fullPath).absolutePath();
    }
    return QString();
}

QString ImportNodeBase::projectName() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectName() : QString();
}

// ============================================================================
// 统一的执行控制
// ============================================================================

void ImportNodeBase::execute()
{
    // 防止重复执行：如果已经在运行中，直接返回
    if (m_thread != nullptr) {
        return;
    }

    m_stopRequested = false;
    m_generatedOutputPaths.clear();
    m_generatedOutputNames.clear();
    const ProductOutputContract outputContract = productOutputContract(0);
    m_semanticTransactionActive = !outputContract.semanticId.isEmpty() &&
        !outputContract.publishedProductTypes.isEmpty();

    // 如果准备阶段确定加载已存在文件，直接跳转完成，不启动 Worker
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (m_semanticTransactionActive) {
            if (!validateAndRestoreOutput()) {
                onError(QStringLiteral("Existing imported product does not satisfy its semantic identity contract."));
                return;
            }
            setProgress(100);
            finishExecution();
            return;
        }
        setProgress(100);
        onImportFinished();
        return;
    }

    if (m_semanticTransactionActive) {
        const QStringList primaryPaths = getExpectedOutputFilePaths();
        QStringList expectedPaths = primaryPaths;
        expectedPaths.append(getExpectedPreviewFilePaths());
        QString transactionError;
        if (primaryPaths.isEmpty() || !NodeUtils::beginOutputTransaction(projectPath(), getOutputNodeName(),
                expectedPaths, transactionInputPaths(), m_outputTransaction, &transactionError)) {
            onError(transactionError.isEmpty() ? QStringLiteral("Import output transaction could not be prepared.")
                                                : transactionError);
            return;
        }
        m_outputTransaction.executionRevision = executionRevision();
        QMap<QString, QString> provenance;
        provenance.insert(QStringLiteral("producer"), name());
        provenance.insert(QStringLiteral("output_port"), outputContract.semanticId);
        if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
                ProductDescriptor::create(outputContract.publishedProductTypes.first(),
                    outputContract.schemaId, outputContract.schemaVersion,
                    outputContract.publishedState, name(), provenance), &transactionError)) {
            onError(transactionError);
            return;
        }
    }

    // Legacy importers retain their direct persistence path until they declare a product contract.
    if (!m_semanticTransactionActive && m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite) {
        NodeUtils::removeDataNodeFromProject(getProjectContext(), getOutputNodeName());
    }

    setProgress(0);
    setState(ExecutionState::Running);

    InSARLogManager::LogInfo(name(), "execute started.");

    // Call the legacy executeImport() method

    executeImport();
}

bool ImportNodeBase::prepareToStart()
{
    // 自动触发模式下，跳过交互弹窗，默认覆盖处理
    if (isAutoTriggered()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        return true;
    }

    QStringList expectedPaths = getExpectedOutputFilePaths();
    if (expectedPaths.isEmpty()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        return true;
    }

    // 汇总需要校验的文件路径：H5成果文件与其对应的预览JPG缩略图
    QStringList pathsToCheck = expectedPaths;
    for (const QString& h5Path : expectedPaths) {
        QFileInfo fi(h5Path);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        pathsToCheck.append(jpgPath);
    }

    // 弹出弹窗询问用户选择
    m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
        getProjectContext(), getOutputNodeName(), pathsToCheck, nullptr);

    // 用户选择取消时返回 false，阻止框架进入 Running 状态并清空成果数据
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        return false;
    }

    return true;
}

void ImportNodeBase::stopExecution()
{
    m_stopRequested = true;

    // 安全取消并断开 remedyWatcher，防范重新执行或销毁时的野指针和竞态条件
    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }

    // 停止 Worker
    if (m_worker) {
        m_worker->StopProcess();
    }

    // 中断单文件操作中的 isInterruptionRequested() 检查
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
    }
}

bool ImportNodeBase::validateAndRestoreOutput()
{
    const ProductOutputContract outputContract = productOutputContract(0);
    if (!outputContract.semanticId.isEmpty() && !outputContract.publishedProductTypes.isEmpty()) {
        QStringList manifestPaths;
        ProductDescriptor::Ptr descriptor;
        QString identityError;
        const QString nodeName = getOutputNodeName();
        if (!NodeUtils::loadCommittedOutputManifest(projectPath(), nodeName, manifestPaths) ||
            !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), nodeName, descriptor, &identityError) ||
            !validatePublishedDescriptor(outputContract, descriptor).accepted) {
            return false;
        }
        const QStringList expectedPrimaryPaths = getExpectedOutputFilePaths();
        QSet<QString> expectedPrimaryNames;
        for (const QString& path : expectedPrimaryPaths) {
            expectedPrimaryNames.insert(QFileInfo(path).fileName());
        }
        QStringList primaryPaths;
        QStringList jpgPaths;
        for (const QString& path : manifestPaths) {
            if (expectedPrimaryNames.contains(QFileInfo(path).fileName())) primaryPaths.append(path);
            if (QFileInfo(path).suffix().compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0) jpgPaths.append(path);
        }
        if (primaryPaths.isEmpty()) return false;
        QStringList h5Paths;
        for (const QString& path : primaryPaths) {
            if (QFileInfo(path).suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0) {
                h5Paths.append(path);
            }
        }
        if (!h5Paths.isEmpty() && !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) return false;
        m_importedFilePaths = primaryPaths;
        m_importedFiles = std::make_shared<ImportedFileData>(primaryPaths, nodeName);
        m_importedFiles->setProductDescriptor(descriptor);
        setOutputData(0, m_importedFiles);
        if (nPorts(PortType::Out) > 1) {
            m_imageInfo = std::make_shared<ImageInfoData>(jpgPaths);
            QMap<QString, QString> previewProvenance;
            previewProvenance.insert(QStringLiteral("producer"), name());
            previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
            m_imageInfo->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
                QStringLiteral("sat-explorer-product"), 1, ProductState::Committed, name(), previewProvenance));
            setOutputData(1, m_imageInfo);
        }
        return true;
    }
    QStringList expectedPaths = getExpectedOutputFilePaths();
    if (expectedPaths.isEmpty())
        return false;

    // Check that all expected H5 files exist on disk
    for (const QString& path : expectedPaths) {
        if (!QFile::exists(path)) {
            return false;
        }
    }

    // 恢复 Port 0
    m_importedFilePaths = expectedPaths;
    QString nodeName = getOutputNodeName();
    m_importedFiles = std::make_shared<ImportedFileData>(expectedPaths, nodeName);
    setOutputData(0, m_importedFiles);

    // 恢复 Port 1 预览
    QStringList expectedJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;

    for (const QString& h5Path : expectedPaths) {
        QFileInfo fi(h5Path);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        expectedJpgPaths.append(jpgPath);

        if (!NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) {
            missingH5s.append(h5Path);
            missingJpgs.append(jpgPath);
        }
    }

    if (!missingH5s.isEmpty()) {
        // 异步生成预览，先断开前一个 watcher，防止重置或二次运行竞态
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
            m_remedyWatcher.waitForFinished();
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, expectedPaths, expectedJpgPaths, missingH5s, missingJpgs]() {
            QStringList validJpgPaths;
            bool anyFailed = false;

            // 检查本次新生成的 JPG
            for (int i = 0; i < missingJpgs.size(); ++i) {
                if (!NodeUtils::isJpgPreviewCurrent(missingH5s[i], missingJpgs[i])) {
                    anyFailed = true;
                }
            }

            // 收集所有最终有效的 JPG
            for (int i = 0; i < expectedJpgPaths.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(expectedPaths[i], expectedJpgPaths[i])) {
                    validJpgPaths.append(expectedJpgPaths[i]);
                }
            }

            // 只有 JPG 确实存在且生成成功时，才能把路径加入 ImageInfoData
            if (!validJpgPaths.isEmpty()) {
                m_imageInfo = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfo);
            } else {
                m_imageInfo.reset();
                setOutputData(1, nullptr);
            }
            Q_EMIT dataUpdated(1);

            // 明确区分：H5 成果已恢复，但预览失败时，状态显示为 Warning
            if (anyFailed) {
                setLastWarningMessage(QStringLiteral("Import data was restored, but some preview images could not be generated."));
                setState(ExecutionState::Warning);
                InSARLogManager::LogWarning(name(), "Import recovery completed, but some preview JPG files failed to generate.");
            } else {
                setState(ExecutionState::Completed);
            }
        });

        QString type = previewDataType();
        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs, type]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(missingH5s[i], missingJpgs[i])) {
                    continue;
                }
                NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], type);
            }
        });
        m_remedyWatcher.setFuture(future);
    } else {
        QStringList validJpgPaths;
        for (int i = 0; i < expectedJpgPaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(expectedPaths[i], expectedJpgPaths[i])) {
                validJpgPaths.append(expectedJpgPaths[i]);
            }
        }
        m_imageInfo = std::make_shared<ImageInfoData>(validJpgPaths);
        setOutputData(1, m_imageInfo);
    }

    return true;
}

void ImportNodeBase::processAutomatically()
{
    // For import nodes, automatic mode typically doesn't do anything
    // since they need user input to select files
    // But we'll complete automatic execution to keep the state consistent
    completeAutomaticExecution();
}

void ImportNodeBase::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);

    // If switching from Manual to Automatic, we could potentially trigger auto-execution
    // but for import nodes this usually doesn't make sense
    // So we just update the mode
}

// ============================================================================
// 统一的 Worker 槽函数
// ============================================================================

void ImportNodeBase::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void ImportNodeBase::onImportFinished()
{
    if (m_semanticTransactionActive) {
        QString transactionError;
        IApplicationInterface* iface = getProjectContext();
        XMLFile* xml = iface ? iface->projectXml() : nullptr;
        if (m_outputPersistenceFailed || !xml) {
            onThreadError(m_outputPersistenceFailed
                ? QStringLiteral("Imported output persistence failed.")
                : QStringLiteral("Project XML context is unavailable for import output commit."));
            return;
        }
        OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
        if (!commitLease) {
            NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                                QStringLiteral("obsolete execution revision"), xml);
            return;
        }
        if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError)) {
            onThreadError(transactionError);
            return;
        }
        QSet<QString> generatedPrimaryNames;
        for (const QString& path : m_generatedOutputPaths) {
            generatedPrimaryNames.insert(QFileInfo(path).fileName());
        }
        const QDir stagingDirectory(QDir(m_outputTransaction.projectRoot)
            .absoluteFilePath(m_outputTransaction.stagingName));
        QStringList stagedPrimaryPaths;
        for (const QString& name : m_outputTransaction.expectedFileNames) {
            if (generatedPrimaryNames.contains(name)) {
                stagedPrimaryPaths.append(stagingDirectory.absoluteFilePath(name));
            }
        }
        if (stagedPrimaryPaths.isEmpty() || stagedPrimaryPaths.size() != m_generatedOutputNames.size() ||
            !NodeUtils::workerOutputsMatchManifest(stagedPrimaryPaths, m_generatedOutputPaths, &transactionError)) {
            onThreadError(transactionError.isEmpty()
                ? QStringLiteral("Imported output manifest does not match the generated primary products.")
                : transactionError);
            return;
        }
        QStringList finalPaths;
        if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
            !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, xml,
                NodeUtils::getProjectFilePath(_widget), &transactionError)) {
            onThreadError(transactionError);
            return;
        }
        QStringList primaryPaths;
        QStringList jpgPaths;
        for (const QString& path : finalPaths) {
            const QString suffix = QFileInfo(path).suffix();
            if (generatedPrimaryNames.contains(QFileInfo(path).fileName())) primaryPaths.append(path);
            if (suffix.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0) jpgPaths.append(path);
        }
        const QString nodeName = getOutputNodeName();
        NodeUtils::removeDataNodeFromProject(iface, nodeName, false, false);
        for (int i = 0; i < primaryPaths.size(); ++i) {
            const QString relativePath = QStringLiteral("/%1/%2").arg(nodeName, QFileInfo(primaryPaths[i]).fileName());
            if (xml->XMLFile_add_origin(nodeName.toStdString().c_str(),
                    m_generatedOutputNames[i].toStdString().c_str(),
                    relativePath.toStdString().c_str(),
                    m_generatedSatelliteFormat.toStdString().c_str()) < 0) {
                onThreadError(QStringLiteral("Unable to update imported product metadata."));
                return;
            }
        }
        if (!NodeUtils::saveProjectXmlAtomically(xml, NodeUtils::getProjectFilePath(_widget),
                &transactionError) || !NodeUtils::markOutputTransactionMetadataCommitted(
                    m_outputTransaction, &transactionError)) {
            onThreadError(transactionError);
            return;
        }
        NodeUtils::removeDataNodeFromProjectTree(iface, nodeName);
        if (!ImportOutputPersistence::publishToModel(projectModel(), projectName(), nodeName,
                m_generatedOutputNames, primaryPaths, m_generatedDataType)) {
            onThreadError(QStringLiteral("Unable to publish imported products to the project tree."));
            return;
        }
        m_importedFilePaths = primaryPaths;
        m_importedFiles = std::make_shared<ImportedFileData>(primaryPaths, nodeName);
        m_importedFiles->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
        setOutputData(0, m_importedFiles);
        if (nPorts(PortType::Out) > 1) {
            m_imageInfo = std::make_shared<ImageInfoData>(jpgPaths);
            QMap<QString, QString> previewProvenance;
            previewProvenance.insert(QStringLiteral("producer"), name());
            previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
            m_imageInfo->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
                QStringLiteral("sat-explorer-product"), 1, ProductState::Committed, name(), previewProvenance));
            setOutputData(1, m_imageInfo);
        }
        if (iface) iface->refreshProjectTree();
        if (m_thread) { m_thread->quit(); m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; }
        if (m_worker) { m_worker->deleteLater(); m_worker = nullptr; }
        finishExecution();
        return;
    }

    if (m_outputPersistenceFailed) {
        onThreadError(QStringLiteral("Unable to save imported outputs to the project."));
        return;
    }

    // 更新导入文件路径
    m_importedFilePaths = m_generatedOutputPaths.isEmpty()
        ? getExpectedOutputFilePaths()
        : m_generatedOutputPaths;
    QString nodeName = getOutputNodeName();

    // 装载 Port 0 数据
    if (!m_importedFilePaths.isEmpty() && !nodeName.isEmpty()) {
        m_importedFiles = std::make_shared<ImportedFileData>(m_importedFilePaths, nodeName);
        setOutputData(0, m_importedFiles);
    }

    // 装载 Port 1 预览数据（过滤并检查实际成功且存在的 JPG 文件）
    if (!m_importedFilePaths.isEmpty()) {
        QStringList validJpgPaths;
        bool anyFailed = false;
        for (const QString& h5Path : m_importedFilePaths) {
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            if (NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) {
                validJpgPaths.append(jpgPath);
            } else {
                anyFailed = true;
            }
        }

        // 只有生成成功的 JPG 才加入 ImageInfoData
        if (!validJpgPaths.isEmpty()) {
            m_imageInfo = std::make_shared<ImageInfoData>(validJpgPaths);
            setOutputData(1, m_imageInfo);
        } else {
            m_imageInfo.reset();
            setOutputData(1, nullptr);
        }

        // 如果主处理已完成，但有 JPG 预览失败，状态设为 Warning，而不显示“全部成功”
        if (anyFailed) {
            setLastWarningMessage(QStringLiteral("Import completed, but some preview images could not be generated."));
            setState(ExecutionState::Warning);
            InSARLogManager::LogWarning(name(), "Import finished, but some preview JPG files failed to generate.");
        }
    }

    // 清理线程
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    InSARLogManager::LogInfo(name(), "execute completed.");

    // 如果上面设置了 Warning，则不要调用 finishExecution 将其覆盖为 Completed
    if (executionState() != ExecutionState::Warning) {
        finishExecution();
    } else {
        // Warning 也要触发完成相关的状态通知
        _progress = 100;
        Q_EMIT progressUpdated(100);
        Q_EMIT executionFinished();
        Q_EMIT executionStateChanged();
        Q_EMIT computingFinished();
        triggerVisualUpdate();
    }
}

void ImportNodeBase::onThreadError(const QString& error)
{
    onError(error);

    // 清理线程
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

void ImportNodeBase::onOutputsGenerated(const QString& dstNode,
                                        const QStringList& outputNames,
                                        const QStringList& outputPaths,
                                        const QString& dataType,
                                        const QString& satelliteFormat)
{
    if (m_semanticTransactionActive) {
        if (dstNode != m_outputTransaction.stagingName) {
            m_outputPersistenceFailed = true;
            return;
        }
        m_generatedOutputNames = outputNames;
        m_generatedOutputPaths = outputPaths;
        m_generatedDataType = dataType;
        m_generatedSatelliteFormat = satelliteFormat;
        return;
    }

    IApplicationInterface* iface = getProjectContext();
    if (!ImportOutputPersistence::persist(projectModel(), projectName(), projectPath(), dstNode,
            outputNames, outputPaths, dataType, satelliteFormat,
            iface ? iface->projectXml() : nullptr)) {
        m_outputPersistenceFailed = true;
        InSARLogManager::LogError(name(), "Unable to save imported outputs to the project.");
        return;
    }

    m_generatedOutputPaths = outputPaths;

    if (iface) {
        iface->refreshProjectTree();
    }
}

void ImportNodeBase::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

// ============================================================================
// 辅助启动函数
// ============================================================================

void ImportNodeBase::startWorker(BaseImportWorker* worker, const std::vector<ImportTask>& tasks)
{
    if (!worker) return;

    m_worker = worker;
    m_outputPersistenceFailed = false;
    m_generatedOutputPaths.clear();
    m_generatedOutputNames.clear();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    // 绑定通用槽函数
    connect(m_worker, &BaseImportWorker::updateProcess, this, &ImportNodeBase::onImportProgress);
    connect(m_worker, &BaseImportWorker::endProcess, this, &ImportNodeBase::onImportFinished);
    connect(m_worker, &BaseImportWorker::errorProcess, this, &ImportNodeBase::onThreadError);
    connect(m_worker, &BaseImportWorker::outputsGenerated,
            this, &ImportNodeBase::onOutputsGenerated);

    m_thread->start();

    bool success = QMetaObject::invokeMethod(m_worker, "import_patch",
        Q_ARG(QString, projectPath()),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, m_semanticTransactionActive ? m_outputTransaction.stagingName : getOutputNodeName()));
    if (!success) {
        qWarning() << "ImportNodeBase::startWorker - Failed to invoke BaseImportWorker::import_patch asynchronously!";
    }
}


// ============================================================================
// Helper methods
// ============================================================================

void ImportNodeBase::onProgressUpdate(int progress, const QString& message)
{
    // Use ExecutableNodeDelegateModel's progress mechanism
    setProgress(progress);
    Q_UNUSED(message);
}

void ImportNodeBase::onError(const QString& error)
{
    if (m_semanticTransactionActive) {
        IApplicationInterface* iface = getProjectContext();
        NodeUtils::abandonOutputTransaction(m_outputTransaction, error, iface ? iface->projectXml() : nullptr);
    }
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void ImportNodeBase::onImportCancelled()
{
    if (m_semanticTransactionActive) {
        IApplicationInterface* iface = getProjectContext();
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"),
                                            iface ? iface->projectXml() : nullptr);
    }
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

IApplicationInterface* ImportNodeBase::getProjectContext() const
{
    return NodeUtils::getProjectContext(_widget);
}

QLabel* ImportNodeBase::createProjectBadge(const QString& projectName)
{
    auto* label = new QLabel();
    label->setObjectName("ProjectBadge");
    label->setText(QStringLiteral(" 📁 当前工程: %1")
        .arg(projectName.isEmpty() ? QStringLiteral("未打开项目") : projectName));
    label->setStyleSheet(
        "QLabel#ProjectBadge {"
        "  background-color: rgba(128, 128, 128, 0.12);"
        "  border: 1px solid rgba(128, 128, 128, 0.2);"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
    );
    return label;
}

} // namespace QtNodes
