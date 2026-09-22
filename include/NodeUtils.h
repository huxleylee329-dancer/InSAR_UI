#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QHash>
#include "QtNodes/internal/ProductContracts.hpp"

#include <functional>
#include <cstdint>
#include <string>

#include <opencv2/core.hpp>

class QWidget;
class IApplicationInterface;
class XMLFile;

class QStandardItem;
class QStandardItemModel;
class QThread;

namespace QtNodes {
class AuxiliaryDemData;
class AuxiliaryDemReferenceData;
class InsarDemData;
}

namespace NodeUtils {

struct AuxiliaryDemBinding {
    QString rasterPath;
    QString identityH5Path;
    QString validMaskPath;
    QString resourceId;
    QString pinnedProvenanceId;
    QString rasterHash;
    QString identityH5Hash;
    QString validMaskHash;
    QString canonicalMetadataHash;
    // Populated only for consumers that explicitly require an orthometric to
    // ellipsoidal conversion. It is never inferred from a directory.
    QString geoidModelPath;
    QString geoidModelHash;
    QString geoidModelId;
    bool fromReference = false;
};

struct DemExecutionSnapshot {
    AuxiliaryDemBinding binding;
    QJsonObject inputGeometry;

    bool isValid() const
    {
        if (binding.resourceId.isEmpty() || inputGeometry.isEmpty()) {
            return false;
        }
        if (!binding.geoidModelPath.isEmpty() || !binding.geoidModelHash.isEmpty() || !binding.geoidModelId.isEmpty()) {
            return !binding.geoidModelPath.isEmpty() && !binding.geoidModelHash.isEmpty() && !binding.geoidModelId.isEmpty();
        }
        return true;
    }
};

struct AuxiliaryDemRegistryEntry {
    QString resourceId;
    QString role;
    QString rasterHash;
    QString identityH5Hash;
    QString validMaskHash;
    qint64 rasterSize = -1;
    qint64 rasterModifiedMs = -1;
    qint64 identityH5Size = -1;
    qint64 identityH5ModifiedMs = -1;
    qint64 validMaskSize = -1;
    qint64 validMaskModifiedMs = -1;
    QString canonicalMetadataHash;
    QString managedRasterPath;
    QString managedIdentityH5Path;
    QString managedValidMaskPath;
    QJsonObject metadata;
    QJsonArray provenanceHistory;
    bool tombstone = false;
    bool legacyUnverified = false;
};

enum class AuxiliaryDemLabelMode {
    FixedResource,
    WorkflowOutput
};

// A workflow label is a project-local alias. Fixed labels immediately bind a
// managed resource; workflow-output labels are declared before their producer
// runs and become ready only when that exact producer publishes a new resource.
struct AuxiliaryDemLabelBinding {
    QString label;
    QString resourceId;
    QString pinnedProvenanceId;
    AuxiliaryDemLabelMode mode = AuxiliaryDemLabelMode::FixedResource;
    QString producerIdentity;
    QString expectedProductType;

    bool isPlanned() const
    {
        return mode == AuxiliaryDemLabelMode::WorkflowOutput &&
               (resourceId.isEmpty() || pinnedProvenanceId.isEmpty());
    }
};

enum class ResourceChangeKind {
    ProvenanceAdded,
    Tombstoned,
    Removed,
    ContentIntegrityFailed,
    PinnedProvenanceMissing,
    ExplicitRebind
};

using ResourceChangeCallback = std::function<void(const QString&, const QString&, ResourceChangeKind)>;
using AuxiliaryDemLabelTableChangedCallback = std::function<void()>;
using AuxiliaryDemLabelReboundCallback = std::function<void(const QString&)>;

bool loadAuxiliaryDemRegistry(const QString& projectRoot,
                              QMap<QString, AuxiliaryDemRegistryEntry>& entries,
                              QString* errorMessage = nullptr);
bool mergeAuxiliaryDemRegistryEntry(const QString& projectRoot,
                                    const AuxiliaryDemRegistryEntry& entry,
                                    QString* errorMessage = nullptr);
bool tombstoneAuxiliaryDemResource(const QString& projectRoot,
                                   const QString& resourceId,
                                   QString* errorMessage = nullptr);
bool removeAuxiliaryDemRegistryEntry(const QString& projectRoot,
                                     const QString& resourceId,
                                     QString* errorMessage = nullptr);
// 清理 .dem_resources 中按内容寻址的受管 DEM 资源目录，仅保留最近 keepCount 份。
// 供在每次成功安装/提交新资源之后调用，防止目录无限膨胀（每份约 500MB）。
void pruneAuxiliaryDemResources(const QString& projectRoot, int keepCount = 3);
// 确保工程目录下已安装官方 EGM96 大地水准面模型及注册清单（若缺失或哈希不符则从内嵌资源释放）
bool ensureProjectGeoidModelInstalled(const QString& projectRoot, QString* errorMessage = nullptr);
QString normalizedDemLabel(const QString& label);
bool loadAuxiliaryDemLabels(const QString& projectRoot,
                            QMap<QString, AuxiliaryDemLabelBinding>& labels,
                            QString* errorMessage = nullptr);
bool bindAuxiliaryDemLabel(const QString& projectRoot,
                           const AuxiliaryDemLabelBinding& binding,
                           bool explicitRebind,
                           QString* errorMessage = nullptr);
bool declareWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                      const QString& label,
                                      const QString& producerIdentity,
                                      bool explicitConvert,
                                      QString* errorMessage = nullptr);
bool activateWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                       const QString& currentLabel,
                                       const QString& nextLabel,
                                       const QString& producerIdentity,
                                       bool explicitConvert,
                                       QString* errorMessage = nullptr);
bool restoreWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                      const QString& label,
                                      const QString& producerIdentity,
                                      bool claimLegacyProducer,
                                      QString* errorMessage = nullptr);
bool invalidateWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                         const QString& label,
                                         const QString& producerIdentity,
                                         QString* errorMessage = nullptr);
// 使流程 DEM 标签失效：清空其 resourceId/pinnedProvenanceId，进入 planned 态。
// 与 invalidateWorkflowAuxiliaryDemLabel 不同，本函数不校验 producer 身份，
// 供标签清理/对账等需要"无条件失效"的场景使用；仅对 WorkflowOutput 模式标签生效，
// 标签不存在时视为成功（幂等）。
bool invalidateWorkflowAuxiliaryDemLabelBinding(const QString& projectRoot,
                                                const QString& requestedLabel,
                                                QString* errorMessage = nullptr);

// 对账流程 DEM 标签：declaredLabels 为当前工作流中实际声明的 (label -> producerIdentity) 映射。
// 对 .dem_resource_labels.json 中所有未被 declaredLabels 声明（即 label 与 producerIdentity 均不一致）的
// WorkflowOutput 标签清除资源绑定（进入 planned 态）。幂等。返回被失效的标签名列表。
QStringList reconcileWorkflowAuxiliaryDemLabels(const QString& projectRoot,
                                                const QMap<QString, QString>& declaredLabels,
                                                QString* errorMessage = nullptr);
bool resolveWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                      const QString& label,
                                      const QString& producerIdentity,
                                      const QString& resourceId,
                                      const QString& pinnedProvenanceId,
                                      QString* errorMessage = nullptr);
void registerPendingAuxiliaryDemLabel(const AuxiliaryDemLabelBinding& binding);
bool resolveAuxiliaryDemLabel(const QString& projectRoot,
                              const QString& label,
                              AuxiliaryDemBinding& binding,
                              QString* errorMessage = nullptr,
                              const QJsonObject& inputGeometry = QJsonObject(),
                              bool requireGeoidModel = false);
void registerResourceChangeCallback(const ResourceChangeCallback& callback);
void registerAuxiliaryDemLabelTableChangedCallback(const AuxiliaryDemLabelTableChangedCallback& callback);
void registerAuxiliaryDemLabelReboundCallback(const AuxiliaryDemLabelReboundCallback& callback);
// 主动触发标签表变化通知（供工作流整体加载完成后驱动各消费者刷新下拉列表）
void emitAuxiliaryDemLabelTableChanged();
void publishResourceChange(const QString& resourceId,
                           const QString& provenanceId,
                           ResourceChangeKind kind);

bool resolveAuxiliaryDemBinding(const QString& projectRoot,
                                const QtNodes::AuxiliaryDemData& data,
                                AuxiliaryDemBinding& binding,
                                QString* errorMessage = nullptr,
                                const QJsonObject& inputGeometry = QJsonObject(),
                                bool requireGeoidModel = false);
bool resolveAuxiliaryDemBinding(const QString& projectRoot,
                                const QtNodes::AuxiliaryDemReferenceData& data,
                                AuxiliaryDemBinding& binding,
                                QString* errorMessage = nullptr,
                                const QJsonObject& inputGeometry = QJsonObject(),
                                bool requireGeoidModel = false);
QJsonObject inputGeometryFromProductDescriptor(const QtNodes::ProductDescriptor::Ptr& descriptor);
bool revalidateAuxiliaryDemBinding(const QString& projectRoot,
                                   const QtNodes::AuxiliaryDemData* entity,
                                   const QtNodes::AuxiliaryDemReferenceData* reference,
                                   AuxiliaryDemBinding& binding,
                                   QString* errorMessage = nullptr,
                                   const AuxiliaryDemBinding* expectedBinding = nullptr,
                                   const QJsonObject& inputGeometry = QJsonObject(),
                                   bool requireGeoidModel = false);
bool revalidateDemExecutionSnapshot(const QString& projectRoot,
                                    const QtNodes::AuxiliaryDemData* entity,
                                    const QtNodes::AuxiliaryDemReferenceData* reference,
                                    const DemExecutionSnapshot& snapshot,
                                    const QJsonObject& currentInputGeometry,
                                    AuxiliaryDemBinding& binding,
                                    QString* errorMessage = nullptr);
bool resolveInsarDemProduct(const QtNodes::InsarDemData& data,
                            QStringList& h5Paths,
                            QString* errorMessage = nullptr);

QMutex* getHdf5Mutex();

class Hdf5Locker {
public:
    // timeoutMs 默认为 -1（代表阻塞式死等）
    // UI 读取时建议指定合理的超时（如 50ms）以防界面卡死
    Hdf5Locker(const QString& filePath, int timeoutMs = -1);
    Hdf5Locker(const std::string& filePath, int timeoutMs = -1);
    // 无参调用：取进程级 HDF5 全局锁。分段等待并周期上报等待时长，
    // 但不会因停止请求放行 —— 106 个无参调用点里有 103 个不检查 isLocked()，
    // 无锁访问 HDF5 会造成库级数据竞争，比多等一会儿严重得多。
    Hdf5Locker();
    ~Hdf5Locker();

    bool isLocked() const { return m_isLocked; }

private:
    QMutex* m_mutex;
    bool m_isLocked;
};

/**
 * @brief Traverses widgets to find the application context (WorkspaceUI/MainWindow)
 * @param widget The node widget or any reference widget
 * @return Pointer to IApplicationInterface or nullptr
 */
IApplicationInterface* getProjectContext(QWidget* widget);

/**
 * @brief Remove a DataNode entry from the project tree model and XML file.
 *
 * Call this when a node's output name changes so the old entry doesn't
 * remain as a stale reference (which would prevent orphan detection).
 *
 * @param iface  Project context (IApplicationInterface*)
 * @param oldNodeName  The old DataNode name to remove
 */
void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName,
                               bool saveXmlImmediately = true,
                               bool updateTreeImmediately = true);
void removeDataNodeFromProjectTree(IApplicationInterface* iface, const QString& nodeName);

/**
 * @brief 向工程 XML 中安全添加数据节点 (Origin 类型)，自适应内存同步与磁盘保存
 */
bool addOriginNodeToProjectXml(IApplicationInterface* iface,
                               const QString& nodeName,
                               const QString& displayName,
                               const QString& relativePath,
                               const QString& tag);

/**
 * @brief 向工程 XML 中安全添加数据节点 (SBAS 类型)，自适应内存同步与磁盘保存
 */
bool addSBASNodeToProjectXml(IApplicationInterface* iface,
                             const QString& nodeName,
                             const QString& dataName,
                             const QString& relativePath);

enum class OverwriteResult {
    NoConflict,
    Overwrite,
    LoadExisting,
    Cancel
};

// A node-owned, same-volume output transaction. The final directory is never
// modified until every staged artifact has been verified.
struct OutputTransaction {
    enum class Stage {
        Inactive,
        StagingCreatePrepared,
        StagingPrepared,
        StagingValidated,
        BackupMovePrepared,
        BackupMoved,
        PromotionPrepared,
        FinalPromoted,
        MetadataCommitPrepared,
        MetadataCommitted,
        Completed,
        Failed
    };

    QString projectRoot;
    // Canonical identity of the active project XML captured before staging.
    // Metadata commit and recovery must never switch to another root XML.
    QString projectXmlPath;
    QString nodeName;
    QString runId;
    QString transactionId;
    QString resourceAction;
    QString installedPath;
    QString resourceStagingPath;
    QString resourceRegistryBackupPath;
    QString resourceRegistryBackupHash;
    QString resourceRegistryCommittedHash;
    QString baseRegistryHash;
    QString baseXmlHash;
    QString newMetadataHash;
    QString baseRegistryGeneration;
    QString baseXmlGeneration;
    QString newRegistryGeneration;
    QString newMetadataGeneration;
    QJsonObject provenanceDelta;
    QString provenanceManifestPath;
    QStringList dependencyTransactions;
    bool provenanceOnlyUpdate = false;
    // Persisted before modifying the registry so recovery also covers a
    // process failure between mergeAuxiliaryDemRegistryEntry() and its hash.
    bool resourceRegistryMutationPrepared = false;
    bool resourceRegistryCommitted = false;
    QString stagingName;
    QString backupName;
    QString journalPath;
    QString metadataXmlName;
    QString metadataBackupName;
    QStringList expectedFileNames;
    QStringList inputPaths;
    QJsonArray inputFingerprints;
    QJsonObject productDescriptor;
    QJsonObject previousFinalManifest;
    QJsonObject previousCommittedJournal;
    std::uint64_t executionRevision = 0;
    bool hasPreviousFinal = false;
    bool backupCleanupDeferred = false;
    bool metadataBackupReady = false;
    // Runtime-only lease for the project XML metadata commit critical section.
    bool metadataCommitLockHeld = false;
    // Worker 在写入阶段已算好的产物哈希（按输出文件名），供 validateStagedOutputTransaction
    // 复用，避免最终化阶段再次整文件读取。不参与事务序列化。
    QHash<QString, QByteArray> precomputedOutputHashes;
    Stage stage = Stage::Inactive;
};

struct OutputTransactionRecoveryInfo {
    bool transactionRecovered = false;
    bool projectXmlRestored = false;
};

// Captures ordered input metadata snapshots. Each entry contains the
// normalized path, size, and modification time.
QJsonArray fingerprintInputPaths(const QStringList& paths);

bool beginOutputTransaction(const QString& projectRoot,
                            const QString& nodeName,
                            const QStringList& expectedFinalPaths,
                            const QStringList& inputPaths,
                            OutputTransaction& transaction,
                            QString* errorMessage = nullptr,
                            OutputTransactionRecoveryInfo* recoveryInfo = nullptr,
                            const QString& projectXmlPath = QString());
// Performs only provably safe rollback/cleanup for an interrupted transaction.
// Ambiguous states remain isolated and return false.
bool recoverOutputTransaction(const QString& projectRoot,
                              const QString& nodeName,
                              QString* errorMessage = nullptr);
// 计算文件的 SHA256（小端十六进制）。供 Worker 在写入阶段预计算大文件哈希，
// 避免最终化阶段再次整文件读取。
QByteArray fileSha256(const QString& path);
// 向 H5 写入 product descriptor（semantic_product_descriptor 数据集）。
// 供 Worker 在写入阶段完成描述写入后据此预计算 H5 哈希。
bool writeProductDescriptorToH5(const QString& filePath, const QJsonObject& descriptor,
                                QString* errorMessage = nullptr);
bool validateStagedOutputTransaction(OutputTransaction& transaction,
                                     QString* errorMessage = nullptr);
bool setOutputTransactionProductDescriptor(OutputTransaction& transaction,
                                           const QtNodes::ProductDescriptor::Ptr& descriptor,
                                           QString* errorMessage = nullptr);
// Verifies that every staged .h5 output contains each required non-empty
// dataset. Non-H5 artifacts in a transaction are validated by the generic
// transaction contract but are not opened as H5 files here.
bool validateStagedH5Datasets(const OutputTransaction& transaction,
                              const QStringList& requiredDatasets,
                              QString* errorMessage = nullptr);
bool promoteOutputTransaction(OutputTransaction& transaction,
                              QStringList& finalPaths,
                              QString* errorMessage = nullptr);
// Completes a transaction whose caller has no project XML metadata to commit.
// The staged files must already have been validated and promoted successfully.
bool completeOutputTransactionWithoutMetadata(OutputTransaction& transaction,
                                              QString* errorMessage = nullptr);
// Completes a transaction that only installs or repairs a managed auxiliary
// DEM resource. The caller must have persisted the resource/registry delta
// before making filesystem changes. No node output directory is promoted.
bool completeAuxiliaryDemResourceTransaction(OutputTransaction& transaction,
                                             QString* errorMessage = nullptr);
// Must be called before mutating the in-memory project XML. It persists a
// transaction-owned XML backup so metadata and promoted files can roll back together.
bool prepareOutputTransactionMetadataCommit(OutputTransaction& transaction,
                                            XMLFile* xml,
                                            const QString& xmlPath,
                                            QString* errorMessage = nullptr);
bool markOutputTransactionMetadataCommitted(OutputTransaction& transaction,
                                            QString* errorMessage = nullptr);
void abandonOutputTransaction(OutputTransaction& transaction,
                              const QString& reason = QString(),
                              XMLFile* xml = nullptr);
bool loadCommittedOutputManifest(const QString& projectRoot,
                                 const QString& nodeName,
                                 QStringList& outputPaths,
                                 QString* errorMessage = nullptr);
// Strict read-only committed snapshot loader for diagnostics/Detail View.
// Unlike loadCommittedOutputManifest(), this never performs transaction
// recovery, cleanup, promotion, or journal mutation.
bool loadCommittedOutputManifestReadOnly(const QString& projectRoot,
                                         const QString& nodeName,
                                         QStringList& outputPaths,
                                         QString& runId,
                                         std::uint64_t& executionRevision,
                                         int& manifestVersion,
                                         QString* errorMessage = nullptr);
bool loadCommittedOutputProductDescriptor(const QString& projectRoot,
                                          const QString& nodeName,
                                          QtNodes::ProductDescriptor::Ptr& descriptor,
                                          QString* errorMessage = nullptr);
// Validates the persisted identity record for one H5 artifact without scanning
// directories. External files with no committed identity record are rejected.
bool validateH5Identity(const QString& h5Path,
                        const QtNodes::ProductDescriptor::Ptr& expectedDescriptor,
                        QtNodes::ProductDescriptor::Ptr* observedDescriptor = nullptr,
                        QString* errorMessage = nullptr);
bool validateH5Identities(const QStringList& h5Paths,
                          const QtNodes::ProductDescriptor::Ptr& expectedDescriptor,
                          QString* errorMessage = nullptr);
// Returns the run identifier bound to a committed output manifest. Callers
// should still use loadCommittedOutputManifest to obtain validated paths.
bool loadCommittedOutputManifestRunId(const QString& projectRoot,
                                      const QString& nodeName,
                                      QString& runId,
                                      QString* errorMessage = nullptr);
// Verifies that worker-reported files form a one-to-one filename mapping to a
// transaction-validated manifest. Worker paths may point at staging while the
// manifest paths point at final, so directory components are intentionally ignored.
bool workerOutputsMatchManifest(const QStringList& manifestPaths,
                                const QStringList& workerPaths,
                                QString* errorMessage = nullptr);
bool saveProjectXmlAtomically(XMLFile* xml, const QString& xmlPath,
                              QString* errorMessage = nullptr);
bool persistOutputTransactionState(OutputTransaction& transaction,
                                   QString* errorMessage = nullptr);

/**
 * @brief Checks if a node with the given name exists in the project tree,
 *        and if any of the physical files exist on disk.
 *        If so, prompts the user for confirmation to overwrite/delete or load existing.
 * @param iface  Project context (IApplicationInterface*)
 * @param nodeName  The node name to check in the project tree
 * @param filePaths  List of physical file paths to check for existence
 * @param parent  Optional parent widget for the QMessageBox
 * @return OverwriteResult indicating the user's choice
 */
OverwriteResult checkAndPromptOverwrite(IApplicationInterface* iface, const QString& nodeName, const QStringList& filePaths, QWidget* parent = nullptr);

/**
 * @brief 物理删除指定路径列表中的文件（若是 .h5 则一并删除同名 .jpg 预览图）
 * @return 若存在的文件无法删除则返回 false
 */
bool removeOutputFiles(const QStringList& filePaths);

/**
 * @brief 从 H5 科学数据文件中提取幅值并生成 JPG 预览图（自动进行超大图降采样）
 * @param h5Path H5文件路径
 * @param jpgPath 输出JPG路径
 * @param type 数据类型，支持 "complex"（复数SLC）和 "phase"（相位）
 * @return 是否生成成功
 */
bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type = "complex");

// 直接从内存中的高程矩阵生成 DEM 预览 JPG（NoData 像元着黑），
// 供 Worker 在写入输出后复用矩阵，避免最终化阶段再从 H5 全量读取。
bool generateDemJpgFromMat(const cv::Mat& dem, const QString& jpgPath);

// Validates and publishes a completed temporary JPG without exposing a partial target file.
bool replaceJpgPreviewAtomically(const QString& temporaryJpgPath, const QString& jpgPath);

// A valid preview must represent every required input used to render it.
bool isJpgPreviewCurrent(const QStringList& inputPaths, const QString& jpgPath);

// Convenience overload for previews rendered from a single H5 source.
bool isJpgPreviewCurrent(const QString& h5Path, const QString& jpgPath);

/**
 * @brief 从 H5 科学数据文件中提取幅值并生成 JPG 预览图，带进度回调接口
 * @param h5Path H5文件路径
 * @param jpgPath 输出JPG路径
 * @param type 数据类型，支持 "complex"（复数SLC）和 "phase"（相位）
 * @param cb 进度回调函数，参数为已处理行数和总行数
 * @return 是否生成成功
 */
bool generateJpgPreviewFromH5WithProgress(const QString& h5Path, const QString& jpgPath, const QString& type, std::function<void(int, int)> cb);

/**
 * @brief 在项目模型指定列中精确查找首个匹配项
 * @return 模型为空、文本为空或未找到匹配项时返回 nullptr
 */
QStandardItem* findFirstModelItem(
    QStandardItemModel* model,
    const QString& text,
    int column = 0
);

/**
 * @brief 查找或创建项目树节点，并根据 Rank 自动排序插入
 * @param project 项目根节点
 * @param nodeName 节点名称
 * @param rankType 排序级名称（如 "complex-0.0" 等）
 * @param iconPath 节点图标路径，若为空则使用默认的 FOLDER_ICON
 * @return 查找到或创建出的 QStandardItem 指针
 */
QStandardItem* findOrCreateProjectNode(
    QStandardItem* project,
    const QString& nodeName,
    const QString& rankType,
    const QString& iconPath = "",
    bool* created = nullptr
);

/**
 * @brief 查找或在父节点下创建子项（数据叶子节点，支持第二列存储路径）
 * @param parent 父节点
 * @param childName 子项名称（文件名）
 * @param tooltip 工具提示信息（数据类型，如 "complex" / "phase" 等）
 * @param h5Path 第二列关联的数据路径
 * @param iconPath 子项图标路径，若为空则使用默认的 IMAGEDATA_ICON
 * @param created 输出参数，指示是否是新建的节点
 * @return 查找到或创建出的 QStandardItem 指针
 */
QStandardItem* findOrCreateChildItem(
    QStandardItem* parent,
    const QString& childName,
    const QString& tooltip,
    const QString& h5Path,
    const QString& iconPath = "",
    bool* created = nullptr
);

/**
 * @brief 从 H5 文件中读取 cv::Mat 矩阵数据（带自动线程锁）
 * @param filePath H5 文件路径
 * @param dataset 数据集名称
 * @param mat 输出的 cv::Mat 矩阵
 * @param targetType 期望转换的 OpenCV 矩阵类型（如 CV_64F、CV_32F 等），默认为 -1 表示不作转换
 * @param errMsg 可选的错误信息输出指针
 * @return 是否读取成功
 */
bool readMatFromH5(const QString& filePath,
                   const QString& dataset,
                   cv::Mat& mat,
                   int targetType = -1,
                   QString* errMsg = nullptr);

// Lightweight H5 dataset metadata probe. It opens the dataset and reads its
// dimensions without materializing raster data.
bool probeH5DatasetMetadata(const QString& filePath,
                            const QString& dataset,
                            int* rows = nullptr,
                            int* columns = nullptr,
                            QString* errMsg = nullptr);

/**
 * @brief 从 H5 文件中读取标量数据（重载形式，支持 int, double, float, qint64）
 */
bool readScalarFromH5(const QString& filePath, const QString& dataset, int& value, QString* errMsg = nullptr);
bool readScalarFromH5(const QString& filePath, const QString& dataset, double& value, QString* errMsg = nullptr);
bool readScalarFromH5(const QString& filePath, const QString& dataset, float& value, QString* errMsg = nullptr);
bool readScalarFromH5(const QString& filePath, const QString& dataset, qint64& value, QString* errMsg = nullptr);

/**
 * @brief 从 H5 文件中读取字符串数据
 */
bool readStringFromH5(const QString& filePath,
                      const QString& dataset,
                      std::string& out,
                      QString* errMsg = nullptr);

bool writeStringToH5(const QString& filePath,
                     const QString& dataset,
                     const std::string& value,
                     QString* errMsg = nullptr);

// Sentinel-1 geometry coefficients use a versioned row/column scene-dimension
// contract.  Non-Sentinel products are accepted unchanged; Sentinel products
// without the contract must be re-imported with the corrected importer.
bool validateSentinelGeometryContract(const QString& filePath,
                                      QString* errMsg = nullptr);

// Validates the source-path contract on an input H5 and verifies that the
// derived H5 already contains the same source paths. Legacy inputs are
// accepted only when both source paths are valid UTF-8.
bool copySourcePathMetadata(const QString& inputPath,
                            const QString& outputPath,
                            QString* errMsg = nullptr);
// Creates UTF-8/v2 source-path metadata on a fresh derived H5.
bool writeSourcePathMetadata(const QString& outputPath,
                             const std::string& source1,
                             const std::string& source2,
                             QString* errMsg = nullptr);

// Copies the phase-processing contract when the input provides one. Legacy
// products without a contract remain readable, but are not upgraded implicitly.
bool copyPhaseProcessingMetadata(const QString& inputPath,
                                 const QString& outputPath,
                                 QString* errMsg = nullptr);

// Validates the phase-processing contract required by DEM inversion.
bool validateDemPhaseInput(const QString& inputPath, QString* errMsg = nullptr);

// Validates either the strict legacy 1x6 model or the complete v2 candidate
// flat-earth contract. A v2 marker never falls back to legacy coefficients.
bool validateFlatEarthReferenceContract(const QString& inputPath, QString* errMsg = nullptr);

// Validates the v2 per-pixel phase-validity contract. Legacy products without
// the dataset remain compatible. requireAllValid is for consumers that cannot
// represent masked phase samples safely.
bool validatePhaseValidityContract(const QString& inputPath,
                                   bool requireAllValid,
                                   QString* errMsg = nullptr);

// A v2 Goldstein Denoise product carries an independent FFT support raster.
// Non-Denoise and non-Goldstein products intentionally have no such contract.
bool validateDenoiseFilterSupportContract(const QString& inputPath,
                                          QString* errMsg = nullptr);
bool copyDenoiseFilterSupportContract(const QString& inputPath,
                                      const QString& outputPath,
                                      QString* errMsg = nullptr);

// ---------------------------------------------------------------------------
// coherence 数据集的语义标签
//
// 背景：H5 中名为 "coherence" 的数据集在不同来源下含义并不相同：
//   - 干涉形成节点与 Core SBAS 目前写入的是 Utils::phase_axial_concentration() 的结果，
//     即二倍角轴向集中度 R2 = |mean(exp(i*2*phi))|，并非复相干系数 gamma；
//   - Utils::phase_circular_concentration() 给出常规圆统计集中度 R1；
//   - Utils::complex_coherence_demodulated() 给出去参考相位后的真 gamma。
// 三者量纲与阈值标定基准都不同，且无法互相换算（实测表明 R2 = R1^4 仅在
// 高相干区近似成立），因此必须显式标注，不能靠来源推断。
//
// 该标签独立于 phase_processing_schema_version：后者是相位处理契约，
// validateDemPhaseInput() 接受旧的 v1 六项模型和 v2 参考场模型，二者不可混用。
// ---------------------------------------------------------------------------
namespace CoherenceSemantics {
// 去参考相位后的归一化复相干系数 gamma
extern const char* const kComplexGamma;
// 一阶圆统计集中度 R1 = |mean(exp(i*phi))|
extern const char* const kPhaseCircularR1;
// 二倍角轴向集中度 R2 = |mean(exp(i*2*phi))|
extern const char* const kPhaseAxialR2;
// 无标签的存量产品：来源不可穷举，不得静态断言为 R2
extern const char* const kLegacyUnknown;
}  // namespace CoherenceSemantics

// coherence 估计的有效样本支持元数据。valid_sample_count 与 coherence
// 同尺寸，记录每个估算窗口中实际有效的相位样本数；窗口尺寸用于判断是否
// 达到完整支持。旧文件可缺少这三项，读取方必须显式降级而不能假定完整支持。
namespace CoherenceSupport {
extern const char* const kValidSampleCountDataset;
extern const char* const kWindowRangeDataset;
extern const char* const kWindowAzimuthDataset;
}  // namespace CoherenceSupport

// 写入 coherence 语义标签。dataset 名为 "coherence_semantics"。
bool writeCoherenceSemantics(const QString& filePath,
                             const QString& semantics,
                             QString* errMsg = nullptr);

// 读取 coherence 语义标签；缺标签时返回 kLegacyUnknown 并返回 true。
// 注意：缺标签只表示“未标注”，不表示 R2。
bool readCoherenceSemantics(const QString& filePath,
                            QString& semantics,
                            QString* errMsg = nullptr);

// 在派生产品间传播语义标签。输入无标签时写入 kLegacyUnknown，
// 避免派生链上出现“上游未标注、下游被误当作已标注”的空档。
bool copyCoherenceSemantics(const QString& inputPath,
                            const QString& outputPath,
                            QString* errMsg = nullptr);

// 将语义标签转为界面可读的短名称（用于评估面板标题与指标行）。
QString coherenceSemanticsDisplayName(const QString& semantics);

/**
 * @brief 向 H5 文件中写入 cv::Mat 矩阵数据（带自动线程锁）
 */
bool writeMatToH5(const QString& filePath,
                  const QString& dataset,
                  const cv::Mat& mat,
                  QString* errMsg = nullptr);

/**
 * @brief 向 H5 文件中写入标量数据
 */
bool writeScalarToH5(const QString& filePath, const QString& dataset, int value, QString* errMsg = nullptr);
bool writeScalarToH5(const QString& filePath, const QString& dataset, double value, QString* errMsg = nullptr);

/**
 * @brief 获取项目全局 DEM 路径，若未设置则返回默认的项目级缓存路径 (projectDir/.dem_cache)
 */
QString getGlobalDemPath(IApplicationInterface* iface);

/**
 * @brief 设置项目全局 DEM 路径，更新 XML 并可选地弹窗询问以及联动更新所有打开的 DEM 输入框
 */
bool setGlobalDemPath(IApplicationInterface* iface, const QString& path, bool askUser = false);

/**
 * @brief 获取应用程序的配置文件 (Config.ini) 的绝对路径，使其始终位于可执行文件同级目录下
 */
QString getConfigPath();

/**
 * @brief 获取 ONNX 模型的绝对路径（带开发调试回退机制）
 * @param modelName 模型文件名（如 "sar_ship_model0429.onnx"）
 */
QString getModelPath(const QString& modelName);

/**
 * @brief 安全的、带事件循环轮询的非阻塞线程等待函数，防止因日志管道满或阻塞导致的双向死锁
 */
void safeThreadWait(QThread* thread, int timeoutMs = 50);

/**
 * @brief 获取项目工程文件的绝对文件路径 (如 "D:/proj/test.insar")
 */
QString getProjectFilePath(QWidget* widget);

/**
 * @brief 获取项目文件所在的绝对目录路径 (如 "D:/proj")
 */
QString getProjectDirectory(QWidget* widget);

/**
 * @brief 将项目工程文件路径转换为所在的工程目录。如果输入已是目录，则原样返回。
 */
QString projectDirectory(const QString& projectPath);

/**
 * @brief 将 DEM 高程矩阵数据写入 TIF 成果文件
 */
bool writeDemToTif(const QString& tifPath, const cv::Mat& dem, const double* gt, const char* wkt);

// Write the companion validity raster for an auxiliary DEM.  1 is a source
// elevation known to be valid; 0 is missing/NoData.  It deliberately has the
// same grid as dem.tif so it can be audited without resampling.
bool writeDemValidityMaskToTif(const QString& tifPath, const cv::Mat& validMask,
                               const double* gt, const char* wkt);

// Verify that a managed DEM mask is binary, grid-aligned with the DEM, agrees
// with finite/non-NoData elevations and has complete source support.
// DEM-dependent processing must fail closed otherwise.
bool validateDemValidityMaskForScene(const QString& demTifPath,
                                     const QString& validMaskTifPath,
                                     double requiredMinLon,
                                     double requiredMaxLon,
                                     double requiredMinLat,
                                     double requiredMaxLat,
                                     qint64* totalPixelCount = nullptr,
                                     qint64* validPixelCount = nullptr,
                                     QString* errorMessage = nullptr);

// 物理内存字节数；取不到时返回 0。
quint64 physicalMemoryBytes();

/**
 * @brief 工作集内存预算（字节）
 *
 * 约定（默认百分比 + 可配置覆盖）：
 *   [Memory] WorkingSetBudgetPercent = 60    （1~95，非法值忽略并回落到 60）
 *   [Memory] WorkingSetBudgetBytes   = <字节> （>0 时优先于百分比）
 * 物理内存取不到时返回 fallbackBytes。
 *
 * 用途：在昂贵步骤之前判断这一步的工作集是否装得下，超预算即提前失败。
 * 注意：节点自身可裁定的护栏（如 DEM 解算的工作集）用本函数；
 *       DLL 内部的护栏（如 MCF 求解器工作集）必须与 DLL 保持一致，不以本函数取值。
 */
quint64 workingSetBudgetBytes(quint64 fallbackBytes);

// 目录所在卷的可用字节数；取不到时返回 -1。
qint64 availableDiskBytes(const QString& directory);

/**
 * @brief 落盘前的磁盘空间预检
 *
 * 要求：可用空间 >= requiredBytes + 安全余量。
 * 安全余量默认取该卷容量的 5%，可用 Config.ini 覆盖：
 *   [Storage] MinFreePercent = 5       （0~50，非法值忽略并回落到 5）
 *   [Storage] MinFreeBytes   = <字节>  （>0 时优先于百分比）
 * 取不到卷信息时返回 true —— 宁可漏报，也不因探测失败误拒合法任务。
 *
 * requiredBytes 传 0 表示只做「安全余量」检查：适用于**无法廉价估算落盘量**的场景
 *（例如各卫星导入：task.arguments 里可能只有 XML，真正的栅格另有其文件）。
 * 能算准的地方（如逐景复制、DEM 落盘）应传入真实估算值。
 */
bool ensureSufficientDiskSpace(const QString& directory, qint64 requiredBytes, QString* errorMessage = nullptr);

} // namespace NodeUtils
