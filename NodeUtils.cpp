#include "include/NodeUtils.h"
#include "InSARLogManager.h"
#include <gdal_priv.h>
#include <QWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include "include/icon_source.h"
#include <QApplication>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QDir>
#include <QDateTime>
#include <QLineEdit>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "include/IApplicationInterface.h"
#include "include/MainWindow.h"
#include "include/WorkspaceUI.h"
#include "include/InterfaceManager.h"
#include "tinyxml.h"
#include <FormatConversion.h>
#include <Utils.h>
#include <cmath>

#include <QMap>
#include <memory>

namespace NodeUtils {

static QMutex g_hdf5GlobalMutex(QMutex::Recursive);
static QMutex g_fileLocksMapMutex(QMutex::Recursive);
static QMap<QString, std::shared_ptr<QMutex>> g_fileLocks;

namespace {

const char* const kTransactionDirectory = ".node_transactions";
const char* const kOutputManifestFile = ".node_output_manifest.json";

bool isDirectProjectChild(const QString& projectRoot, const QString& name,
                          QString* failureReason = nullptr);
bool writeJsonAtomically(const QString& path, const QJsonObject& object, QString* errorMessage);
QString transactionDirectoryPath(const QString& root);

bool isCompleteJpegFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() < 4) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray start = file.read(2);
    if (!file.seek(info.size() - 2)) {
        return false;
    }
    const QByteArray end = file.read(2);
    return start.size() == 2 && end.size() == 2 &&
        start[0] == static_cast<char>(0xff) && start[1] == static_cast<char>(0xd8) &&
        end[0] == static_cast<char>(0xff) && end[1] == static_cast<char>(0xd9);
}

QString stageName(OutputTransaction::Stage stage)
{
    switch (stage) {
    case OutputTransaction::Stage::StagingCreatePrepared: return QStringLiteral("StagingCreatePrepared");
    case OutputTransaction::Stage::StagingPrepared: return QStringLiteral("StagingPrepared");
    case OutputTransaction::Stage::StagingValidated: return QStringLiteral("StagingValidated");
    case OutputTransaction::Stage::BackupMovePrepared: return QStringLiteral("BackupMovePrepared");
    case OutputTransaction::Stage::BackupMoved: return QStringLiteral("BackupMoved");
    case OutputTransaction::Stage::PromotionPrepared: return QStringLiteral("PromotionPrepared");
    case OutputTransaction::Stage::FinalPromoted: return QStringLiteral("FinalPromoted");
    case OutputTransaction::Stage::MetadataCommitPrepared: return QStringLiteral("MetadataCommitPrepared");
    case OutputTransaction::Stage::MetadataCommitted: return QStringLiteral("MetadataCommitted");
    case OutputTransaction::Stage::Completed: return QStringLiteral("Completed");
    case OutputTransaction::Stage::Failed: return QStringLiteral("Failed");
    default: return QStringLiteral("Inactive");
    }
}

bool journalNamesAreSafe(const QDir& root, const QString& nodeName, const QJsonObject& journal)
{
    const QString stagingName = journal.value(QStringLiteral("stagingDirectory")).toString();
    const QString backupName = journal.value(QStringLiteral("backupDirectory")).toString();
    const QString metadataXmlName = journal.value(QStringLiteral("metadataXmlName")).toString();
    const QString metadataBackupName = journal.value(QStringLiteral("metadataBackupName")).toString();
    const bool metadataNamesPresent = !metadataXmlName.isEmpty() || !metadataBackupName.isEmpty();
    return journal.value(QStringLiteral("nodeName")).toString() == nodeName &&
           isDirectProjectChild(root.absolutePath(), nodeName) &&
           isDirectProjectChild(root.absolutePath(), stagingName) &&
           isDirectProjectChild(root.absolutePath(), backupName) &&
           stagingName.startsWith(QStringLiteral(".%1.staging-").arg(nodeName), Qt::CaseInsensitive) &&
           backupName.startsWith(QStringLiteral(".%1.backup-").arg(nodeName), Qt::CaseInsensitive) &&
           (!metadataNamesPresent ||
            (isDirectProjectChild(root.absolutePath(), metadataXmlName) &&
             !metadataBackupName.contains('/') && !metadataBackupName.contains('\\') &&
             metadataBackupName.startsWith(QStringLiteral(".%1.metadata-backup-").arg(nodeName), Qt::CaseInsensitive)));
}

bool updateJournalRecoveryState(const QString& journalPath, QJsonObject journal,
                                const QString& stage, const QString& action,
                                QString* errorMessage)
{
    journal.insert(QStringLiteral("stage"), stage);
    journal.insert(QStringLiteral("recoveryAction"), action);
    journal.insert(QStringLiteral("recoveredAtMs"),
                   static_cast<double>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()));
    return writeJsonAtomically(journalPath, journal, errorMessage);
}

bool isDirectProjectChild(const QString& projectRoot, const QString& name,
                          QString* failureReason)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.contains('/') || name.contains('\\')) {
        if (failureReason) *failureReason = QStringLiteral("node name is empty, reserved, or contains a path separator");
        return false;
    }
    const QDir root(projectRoot);
    const QString rootPath = QDir::cleanPath(QDir::fromNativeSeparators(root.absolutePath()));
    const QFileInfo rootInfo(rootPath);
    const QFileInfo candidate(root.absoluteFilePath(name));
    const QString candidatePath = QDir::cleanPath(QDir::fromNativeSeparators(candidate.absoluteFilePath()));
    const QString rootPrefix = rootPath.endsWith(QLatin1Char('/')) ? rootPath : rootPath + QLatin1Char('/');
    if (!candidatePath.startsWith(rootPrefix, Qt::CaseInsensitive)) {
        if (failureReason) *failureReason = QStringLiteral("candidate path is not a direct child of the project root");
        return false;
    }

    // Existing reparse points may redirect an apparently direct child outside
    // the project. New staging names do not exist yet and are safe once their
    // lexical target has passed the checks above.
    if (candidate.exists() && candidate.isSymLink()) {
        if (failureReason) *failureReason = QStringLiteral("existing output directory is a symbolic link");
        return false;
    }
    if (rootInfo.exists() && rootInfo.isSymLink()) {
        if (failureReason) *failureReason = QStringLiteral("project root is a symbolic link");
        return false;
    }
#ifdef Q_OS_WIN
    const auto isReparsePoint = [](const QString& path) {
        const DWORD attributes = ::GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    };
    if (candidate.exists() && isReparsePoint(candidatePath)) {
        if (failureReason) *failureReason = QStringLiteral("existing output directory is a Windows reparse point");
        return false;
    }
    if (rootInfo.exists() && isReparsePoint(rootPath)) {
        if (failureReason) *failureReason = QStringLiteral("project root is a Windows reparse point");
        return false;
    }
#endif
    return true;
}

bool writeJsonAtomically(const QString& path, const QJsonObject& object, QString* errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot open transaction record: %1").arg(path);
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit transaction record: %1").arg(path);
        return false;
    }
    return true;
}

QJsonArray fingerprintInputs(const QStringList& paths)
{
    QJsonArray fingerprints;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        QJsonObject item;
        item.insert(QStringLiteral("path"), QDir::cleanPath(info.absoluteFilePath()));
        item.insert(QStringLiteral("size"), static_cast<double>(info.exists() ? info.size() : -1));
        item.insert(QStringLiteral("modifiedMs"), static_cast<double>(info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1));
        fingerprints.append(item);
    }
    return fingerprints;
}

bool fingerprintsMatch(const QJsonArray& expected, const QStringList& paths)
{
    return QJsonDocument(expected).toJson(QJsonDocument::Compact) ==
           QJsonDocument(fingerprintInputs(paths)).toJson(QJsonDocument::Compact);
}

bool validateOutputFile(const QFileInfo& info, QString* errorMessage)
{
    if (!info.isFile() || info.size() <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Missing or empty staged output: %1").arg(info.absoluteFilePath());
        return false;
    }
    // Dataset-level validation remains node-specific because these nodes emit
    // different H5 products. The transaction layer verifies the complete,
    // non-empty candidate set without adding another HDF5 ABI dependency.
    return true;
}

QString metadataBackupPath(const OutputTransaction& transaction)
{
    return QDir(transactionDirectoryPath(transaction.projectRoot)).absoluteFilePath(transaction.metadataBackupName);
}

QString metadataXmlPath(const OutputTransaction& transaction)
{
    return QDir(transaction.projectRoot).absoluteFilePath(transaction.metadataXmlName);
}

bool restoreFileAtomically(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot open transaction XML backup: %1").arg(sourcePath);
        return false;
    }

    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot restore project XML: %1").arg(targetPath);
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray block = source.read(64 * 1024);
        if (block.isEmpty() && source.error() != QFile::NoError) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot read transaction XML backup: %1").arg(sourcePath);
            return false;
        }
        if (!block.isEmpty() && target.write(block) != block.size()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot write restored project XML: %1").arg(targetPath);
            return false;
        }
    }
    if (!target.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit restored project XML: %1").arg(targetPath);
        return false;
    }
    return true;
}

bool restoreMetadataBackup(const OutputTransaction& transaction, XMLFile* xml, QString* errorMessage)
{
    if (!transaction.metadataBackupReady) return true;
    const QString backupPath = metadataBackupPath(transaction);
    const QString xmlPath = metadataXmlPath(transaction);
    const QFileInfo backupInfo(backupPath);
    if (!backupInfo.isFile() || !restoreFileAtomically(backupPath, xmlPath, errorMessage)) {
        return false;
    }
    if (xml) {
        const QByteArray nativePath = QDir::toNativeSeparators(xmlPath).toLocal8Bit();
        if (xml->XMLFile_load(nativePath.constData()) < 0) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot reload restored project XML: %1").arg(xmlPath);
            return false;
        }
    }
    return true;
}

bool persistTransaction(OutputTransaction& transaction, QString* errorMessage)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("runId"), transaction.runId);
    object.insert(QStringLiteral("nodeName"), transaction.nodeName);
    object.insert(QStringLiteral("stage"), stageName(transaction.stage));
    object.insert(QStringLiteral("finalDirectory"), transaction.nodeName);
    object.insert(QStringLiteral("stagingDirectory"), transaction.stagingName);
    object.insert(QStringLiteral("backupDirectory"), transaction.backupName);
    object.insert(QStringLiteral("metadataXmlName"), transaction.metadataXmlName);
    object.insert(QStringLiteral("metadataBackupName"), transaction.metadataBackupName);
    object.insert(QStringLiteral("metadataBackupReady"), transaction.metadataBackupReady);
    object.insert(QStringLiteral("hasPreviousFinal"), transaction.hasPreviousFinal);
    object.insert(QStringLiteral("backupCleanupDeferred"), transaction.backupCleanupDeferred);
    object.insert(QStringLiteral("previousFinalState"), transaction.hasPreviousFinal
        ? QStringLiteral("Present") : QStringLiteral("NoPreviousFinal"));
    if (!transaction.previousFinalManifest.isEmpty()) {
        object.insert(QStringLiteral("previousFinalManifest"), transaction.previousFinalManifest);
    }
    if (!transaction.previousCommittedJournal.isEmpty()) {
        object.insert(QStringLiteral("previousCommittedJournal"), transaction.previousCommittedJournal);
    }
    object.insert(QStringLiteral("inputs"), transaction.inputFingerprints);
    QJsonArray files;
    for (const QString& name : transaction.expectedFileNames) files.append(name);
    object.insert(QStringLiteral("expectedFiles"), files);
    return writeJsonAtomically(transaction.journalPath, object, errorMessage);
}

bool setTransactionStage(OutputTransaction& transaction, OutputTransaction::Stage stage, QString* errorMessage)
{
    transaction.stage = stage;
    return persistTransaction(transaction, errorMessage);
}

QString transactionDirectoryPath(const QString& root)
{
    return QDir(root).absoluteFilePath(QString::fromLatin1(kTransactionDirectory));
}

} // namespace

QMutex* getHdf5Mutex()
{
    return &g_hdf5GlobalMutex;
}

// 获取或创建特定文件的局部锁
static QMutex* getMutexForFile(const QString& filePath)
{
    if (filePath.isEmpty()) return &g_hdf5GlobalMutex;
    
    // 路径标准化：消除斜杠差异、统一小写，避免物理上的同一文件因路径写法差异造成锁失效
    QString normPath = QDir::toNativeSeparators(filePath).toLower();
    
    QMutexLocker mapLocker(&g_fileLocksMapMutex);
    if (!g_fileLocks.contains(normPath)) {
        g_fileLocks[normPath] = std::make_shared<QMutex>(QMutex::Recursive);
    }
    return g_fileLocks[normPath].get();
}

Hdf5Locker::Hdf5Locker()
    : m_mutex(&g_hdf5GlobalMutex)
    , m_isLocked(false)
{
    m_mutex->lock();
    m_isLocked = true;
}

Hdf5Locker::Hdf5Locker(const QString& filePath, int timeoutMs)
    : m_isLocked(false)
{
    m_mutex = getMutexForFile(filePath);
    if (timeoutMs < 0) {
        m_mutex->lock();
        m_isLocked = true;
    } else {
        m_isLocked = m_mutex->tryLock(timeoutMs);
    }
}

Hdf5Locker::Hdf5Locker(const std::string& filePath, int timeoutMs)
    : m_isLocked(false)
{
    m_mutex = getMutexForFile(QString::fromStdString(filePath));
    if (timeoutMs < 0) {
        m_mutex->lock();
        m_isLocked = true;
    } else {
        m_isLocked = m_mutex->tryLock(timeoutMs);
    }
}

Hdf5Locker::~Hdf5Locker()
{
    if (m_isLocked && m_mutex) {
        m_mutex->unlock();
    }
}

IApplicationInterface* getProjectContext(QWidget* widget)
{
    // 1. Try parent widget traversal
    if (widget)
    {
        QWidget* parent = widget->parentWidget();
        while (parent)
        {
            auto* iface = dynamic_cast<IApplicationInterface*>(parent);
            if (iface) {
                return iface;
            }
            parent = parent->parentWidget();
        }
    }

    // 2. Fallback to MainWindow -> workspaceUI
    foreach(QWidget * topLevelWidget, QApplication::topLevelWidgets()) {
        MainWindow* mainWin = qobject_cast<MainWindow*>(topLevelWidget);
        if (mainWin) {
            // Prefer workspaceUI as it's the source of truth for project data
            if (mainWin->workspaceUI()) {
                return mainWin->workspaceUI();
            }
            // Fallback to interface manager's current interface
            if (mainWin->interfaceManager()) {
                auto* iface = mainWin->interfaceManager()->currentInterface();
                if (iface) {
                    return iface;
                }
            }
        }
    }

    return nullptr;
}

void removeDataNodeFromProjectTree(IApplicationInterface* iface, const QString& nodeName)
{
    if (!iface || nodeName.isEmpty()) return;
    QStandardItemModel* model = iface->projectModel();
    const QString projectName = iface->projectName();
    if (!model || projectName.isEmpty()) return;

    QList<QStandardItem*> projectItems = model->findItems(projectName);
    if (projectItems.isEmpty()) {
        for (int row = 0; row < model->rowCount(); ++row) {
            if (QStandardItem* item = model->item(row, 0)) projectItems.append(item);
        }
    }
    for (QStandardItem* projectItem : projectItems) {
        for (int row = projectItem->rowCount() - 1; row >= 0; --row) {
            QStandardItem* nodeItem = projectItem->child(row, 0);
            if (nodeItem && nodeItem->text() == nodeName) {
                projectItem->removeRow(row);
            }
        }
    }
}

void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName,
                                      bool saveXmlImmediately,
                                      bool updateTreeImmediately)
{
    if (!iface || oldNodeName.isEmpty()) return;

    QStandardItemModel* model = iface->projectModel();
    QString projPath = iface->projectPath();
    QString projName = iface->projectName();

    if (projPath.isEmpty() || projName.isEmpty()) return;

    // projectPath() from ImportNodeBase returns QFileInfo(fullPath).absolutePath()
    // i.e. the directory containing the .Insar file.
    // projName is the .Insar filename (e.g. "myproject.Insar")
    // XML path = projPath + "/" + projName  (but projPath may already be the dir)

    if (updateTreeImmediately && model) {
        removeDataNodeFromProjectTree(iface, oldNodeName);
    }

    // 同时从 XML 文件中移除
    // 确定 XML 文件路径。IApplicationInterface 的 projectPath()
    // 返回 .Insar 文件的全路径 (例如 "D:/projects/test.Insar")
    QString xmlPath = projPath;
    // 如果 projPath 不以 projName 结尾，则构建它
    if (!xmlPath.endsWith(projName)) {
        // projPath 为目录，projName 为文件名
        xmlPath = projPath + "/" + projName;
    }

    // 优先获取并修改全局内存中的 XMLFile 实例，确保内存与磁盘实时同步
    XMLFile* xml = iface->projectXml();
    bool isGlobalXml = (xml != nullptr);
    XMLFile localXml;

    if (!isGlobalXml) {
        if (localXml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
            return;
        }
        xml = &localXml;
    }

    // 查找并移除匹配名称的 DataNode 元素
    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (root) {
        for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; ) {
            const char* nameAttr = p->Attribute("name");
            if (nameAttr && QString(nameAttr) == oldNodeName) {
                TiXmlElement* toDelete = p;
                p = p->NextSiblingElement();
                root->RemoveChild(toDelete);
            } else {
                p = p->NextSiblingElement();
            }
        }
    }
    if (saveXmlImmediately) {
        xml->XMLFile_save(xmlPath.toStdString().c_str());
    }
}

bool addOriginNodeToProjectXml(IApplicationInterface* iface,
                               const QString& nodeName,
                               const QString& displayName,
                               const QString& relativePath,
                               const QString& tag)
{
    if (!iface || nodeName.isEmpty()) return false;

    QString projPath = iface->projectPath();
    QString projName = iface->projectName();
    if (projPath.isEmpty() || projName.isEmpty()) return false;

    QString xmlPath = projPath;
    if (!xmlPath.endsWith(projName)) {
        xmlPath = projPath + "/" + projName;
    }

    XMLFile* xml = iface->projectXml();
    bool isGlobalXml = (xml != nullptr);
    XMLFile localXml;

    if (!isGlobalXml) {
        if (localXml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
            return false;
        }
        xml = &localXml;
    }

    int ret = xml->XMLFile_add_origin(
        nodeName.toStdString().c_str(),
        displayName.toStdString().c_str(),
        relativePath.toStdString().c_str(),
        tag.toStdString().c_str()
    );

    if (ret >= 0) {
        xml->XMLFile_save(xmlPath.toStdString().c_str());
        return true;
    }
    return false;
}

bool addSBASNodeToProjectXml(IApplicationInterface* iface,
                             const QString& nodeName,
                             const QString& dataName,
                             const QString& relativePath)
{
    if (!iface || nodeName.isEmpty()) return false;

    QString projPath = iface->projectPath();
    QString projName = iface->projectName();
    if (projPath.isEmpty() || projName.isEmpty()) return false;

    QString xmlPath = projPath;
    if (!xmlPath.endsWith(projName)) {
        xmlPath = projPath + "/" + projName;
    }

    XMLFile* xml = iface->projectXml();
    bool isGlobalXml = (xml != nullptr);
    XMLFile localXml;

    if (!isGlobalXml) {
        if (localXml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
            return false;
        }
        xml = &localXml;
    }

    int ret = xml->XMLFile_add_SBAS(
        nodeName.toStdString().c_str(),
        dataName.toStdString().c_str(),
        relativePath.toStdString().c_str()
    );

    if (ret >= 0) {
        xml->XMLFile_save(xmlPath.toStdString().c_str());
        return true;
    }
    return false;
}

OverwriteResult checkAndPromptOverwrite(IApplicationInterface* iface, const QString& nodeName, const QStringList& filePaths, QWidget* parent)
{
    bool hasConflict = false;
    QStringList conflictDetails;

    // 1. Check if node exists in the project tree
    if (iface && !nodeName.isEmpty()) {
        QStandardItemModel* model = iface->projectModel();
        QString projName = iface->projectName();
        if (model && !projName.isEmpty()) {
            QList<QStandardItem*> projItems = model->findItems(projName);
            if (projItems.isEmpty()) {
                for (int r = 0; r < model->rowCount(); ++r) {
                    QStandardItem* item = model->item(r, 0);
                    if (item) projItems.append(item);
                }
            }
            for (QStandardItem* projItem : projItems) {
                for (int i = 0; i < projItem->rowCount(); ++i) {
                    QStandardItem* nodeItem = projItem->child(i, 0);
                    if (nodeItem && nodeItem->text() == nodeName) {
                        hasConflict = true;
                        conflictDetails.append("- 已存在同名节点: " + nodeName);
                        break;
                    }
                }
                if (hasConflict) break;
            }
        }
    }

    // 2. Check if physical files exist
    QStringList existingFiles;
    bool allFilesExist = !filePaths.isEmpty();
    for (const QString& path : filePaths) {
        if (QFile::exists(path)) {
            existingFiles.append(QFileInfo(path).fileName());
        } else {
            allFilesExist = false;
        }
    }
    
    if (!existingFiles.isEmpty()) {
        existingFiles.removeDuplicates();
        hasConflict = true;
        conflictDetails.append("- 已存在同名文件:\n    " + existingFiles.join("\n    "));
    }

    // 3. Prompt user if conflicts were found
    if (hasConflict) {
        QMessageBox msgBox(parent);
        msgBox.setWindowTitle("冲突处理");
        msgBox.setIcon(QMessageBox::Warning);
        
        QString msg = "检测到冲突：\n\n" + conflictDetails.join("\n\n") + 
                      "\n\n请选择后续操作：\n";
        msgBox.setText(msg);

        QPushButton* overwriteBtn = msgBox.addButton("重新运行并覆盖", QMessageBox::AcceptRole);
        QPushButton* loadBtn = nullptr;
        if (allFilesExist) {
            loadBtn = msgBox.addButton("加载已存在文件", QMessageBox::AcceptRole);
        }
        QPushButton* cancelBtn = msgBox.addButton("取消", QMessageBox::RejectRole);

        msgBox.setDefaultButton(cancelBtn);
        msgBox.exec();

        if (msgBox.clickedButton() == overwriteBtn) {
            return OverwriteResult::Overwrite;
        } else if (loadBtn && msgBox.clickedButton() == loadBtn) {
            return OverwriteResult::LoadExisting;
        } else {
            return OverwriteResult::Cancel;
        }
    }

    return OverwriteResult::NoConflict;
}

bool recoverOutputTransaction(const QString& projectRoot, const QString& nodeName, QString* errorMessage)
{
    QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid output transaction recovery target.");
        return false;
    }
    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath()))
        .absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (!QFileInfo::exists(journalPath)) return true;

    QFile journalFile(journalPath);
    if (!journalFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot inspect interrupted output transaction: %1").arg(journalPath);
        return false;
    }
    const QJsonObject journal = QJsonDocument::fromJson(journalFile.readAll()).object();
    if (!journalNamesAreSafe(root, nodeName, journal)) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction journal has unsafe paths and was left untouched: %1").arg(journalPath);
        return false;
    }

    const QString stage = journal.value(QStringLiteral("stage")).toString();
    const QString stagingName = journal.value(QStringLiteral("stagingDirectory")).toString();
    const QString backupName = journal.value(QStringLiteral("backupDirectory")).toString();
    const bool hasPreviousFinal = journal.value(QStringLiteral("hasPreviousFinal")).toBool(false);
    const QString finalPath = root.absoluteFilePath(nodeName);
    const QString stagingPath = root.absoluteFilePath(stagingName);
    const QString backupPath = root.absoluteFilePath(backupName);
    const auto removeStaging = [&]() {
        return !QDir(stagingPath).exists() || QDir(stagingPath).removeRecursively();
    };
    const auto markFailed = [&](const QString& action) {
        return updateJournalRecoveryState(journalPath, journal, QStringLiteral("Failed"), action, errorMessage);
    };
    const auto restorePreviousCommittedJournal = [&]() {
        const QJsonObject previous = journal.value(QStringLiteral("previousCommittedJournal")).toObject();
        if (previous.isEmpty()) return false;
        if (!journalNamesAreSafe(root, nodeName, previous)) {
            if (errorMessage) *errorMessage = QStringLiteral("Interrupted transaction has an unsafe previous committed journal.");
            return false;
        }
        return writeJsonAtomically(journalPath, previous, errorMessage);
    };

    if (stage == QStringLiteral("Completed") || stage == QStringLiteral("Failed")) return true;

    if (stage == QStringLiteral("MetadataCommitted")) {
        QFile manifestFile(QDir(finalPath).absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is unavailable during recovery.");
            return false;
        }
        const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        if (manifest.value(QStringLiteral("nodeName")).toString() != nodeName ||
            manifest.value(QStringLiteral("runId")).toString() != journal.value(QStringLiteral("runId")).toString()) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest does not match the interrupted transaction.");
            return false;
        }
        QSet<QString> expectedNames;
        for (const QJsonValue& value : journal.value(QStringLiteral("expectedFiles")).toArray()) {
            const QString name = value.toString();
            if (name.isEmpty() || expectedNames.contains(name)) {
                if (errorMessage) *errorMessage = QStringLiteral("Committed transaction has an invalid output manifest contract.");
                return false;
            }
            expectedNames.insert(name);
        }
        const QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
        QSet<QString> actualNames;
        for (const QJsonValue& value : outputs) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString();
            const QFileInfo outputInfo(QDir(finalPath).absoluteFilePath(name));
            if (name.isEmpty() || !expectedNames.contains(name) || actualNames.contains(name) ||
                !validateOutputFile(outputInfo, errorMessage)) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("Committed output manifest is invalid during recovery.");
                }
                return false;
            }
            actualNames.insert(name);
        }
        if (actualNames != expectedNames) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is incomplete during recovery.");
            return false;
        }
        QJsonObject completedJournal = journal;
        completedJournal.remove(QStringLiteral("previousCommittedJournal"));
        if (QDir(backupPath).exists() && !QDir(backupPath).removeRecursively()) {
            completedJournal.insert(QStringLiteral("backupCleanupDeferred"), true);
            InSARLogManager::LogWarning("NodeUtils", QString("Deferred interrupted-transaction backup cleanup: %1").arg(backupName));
        }
        return updateJournalRecoveryState(journalPath, completedJournal, QStringLiteral("Completed"),
                                          QStringLiteral("metadata already committed"), errorMessage);
    }

    if (stage == QStringLiteral("StagingCreatePrepared")) {
        if (QDir(backupPath).exists()) {
            if (errorMessage) *errorMessage = QStringLiteral("Staging creation recovery found an unexpected backup directory.");
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove interrupted staging directory: %1").arg(stagingPath);
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("discarded interrupted staging creation"));
    }

    if (stage == QStringLiteral("StagingPrepared") || stage == QStringLiteral("StagingValidated")) {
        if ((!hasPreviousFinal && QDir(finalPath).exists()) ||
            (hasPreviousFinal && QDir(backupPath).exists())) {
            if (errorMessage) *errorMessage = QStringLiteral("Staging recovery found an unexpected final or backup directory.");
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated staging directory: %1").arg(stagingPath);
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("discarded unpromoted staging output"));
    }

    if (stage == QStringLiteral("BackupMovePrepared") || stage == QStringLiteral("BackupMoved")) {
        if ((!hasPreviousFinal && (QDir(finalPath).exists() || QDir(backupPath).exists())) ||
            (hasPreviousFinal && QDir(finalPath).exists() && QDir(backupPath).exists())) {
            if (errorMessage) *errorMessage = QStringLiteral("Backup recovery found an ambiguous final/backup directory combination.");
            return false;
        }
        if (hasPreviousFinal && !QDir(finalPath).exists() && QDir(backupPath).exists() &&
            !root.rename(backupName, nodeName)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot restore previous final directory from backup.");
            return false;
        }
        if (!QDir(finalPath).exists() && hasPreviousFinal) {
            if (errorMessage) *errorMessage = QStringLiteral("Interrupted backup move cannot be proven or restored.");
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove staging directory after backup recovery.");
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("restored or retained previous final before promotion"));
    }

    if (stage == QStringLiteral("PromotionPrepared")) {
        const bool finalExists = QDir(finalPath).exists();
        const bool stagingExists = QDir(stagingPath).exists();
        if (hasPreviousFinal) {
            if (!QDir(backupPath).exists()) {
                if (errorMessage) *errorMessage = QStringLiteral("Promotion recovery is ambiguous because the previous backup is missing.");
                return false;
            }
            if (finalExists && !stagingExists && !root.rename(nodeName, stagingName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot isolate promoted candidate during rollback.");
                return false;
            }
            if (!QDir(finalPath).exists() && !root.rename(backupName, nodeName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot restore previous final after interrupted promotion.");
                return false;
            }
        } else if (finalExists && !stagingExists) {
            if (!QDir(finalPath).removeRecursively()) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot remove uncommitted promoted candidate.");
                return false;
            }
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated candidate after promotion recovery.");
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("rolled back interrupted promotion before final commit"));
    }

    if (stage == QStringLiteral("MetadataCommitPrepared")) {
        OutputTransaction transaction;
        transaction.projectRoot = root.absolutePath();
        transaction.nodeName = nodeName;
        transaction.metadataXmlName = journal.value(QStringLiteral("metadataXmlName")).toString();
        transaction.metadataBackupName = journal.value(QStringLiteral("metadataBackupName")).toString();
        transaction.metadataBackupReady = journal.value(QStringLiteral("metadataBackupReady")).toBool(false);
        if (transaction.metadataXmlName.isEmpty() || transaction.metadataBackupName.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Metadata recovery record is incomplete.");
            return false;
        }
        if (!restoreMetadataBackup(transaction, nullptr, errorMessage)) {
            return false;
        }

        const bool finalExists = QDir(finalPath).exists();
        const bool stagingExists = QDir(stagingPath).exists();
        if (stagingExists) {
            if (errorMessage) *errorMessage = QStringLiteral("Metadata recovery found an unexpected staging directory.");
            return false;
        }
        if (hasPreviousFinal) {
            if (!QDir(backupPath).exists()) {
                if (errorMessage) *errorMessage = QStringLiteral("Metadata recovery cannot restore the previous output backup.");
                return false;
            }
            if (finalExists && !root.rename(nodeName, stagingName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot isolate output candidate during metadata recovery.");
                return false;
            }
            if (!QDir(finalPath).exists() && !root.rename(backupName, nodeName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot restore the previous output after metadata recovery.");
                return false;
            }
        } else if (finalExists && !QDir(finalPath).removeRecursively()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove uncommitted output candidate after metadata recovery.");
            return false;
        }

        if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated output candidate after metadata recovery.");
            return false;
        }
        if (!transaction.metadataBackupName.isEmpty() &&
            QFileInfo(metadataBackupPath(transaction)).exists() &&
            !QFile::remove(metadataBackupPath(transaction))) {
            InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for interrupted XML backup: %1")
                .arg(transaction.metadataBackupName));
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("restored XML backup and rolled back uncommitted metadata"));
    }

    // FinalPromoted is deliberately not rolled back automatically: the XML
    // replacement may have happened immediately before the process stopped.
    // Without a durable XML backup this state is not provably consistent.
    if (errorMessage) *errorMessage = QStringLiteral("Interrupted output transaction is ambiguous at stage %1; output remains isolated until controlled recovery.")
        .arg(stage.isEmpty() ? QStringLiteral("unknown") : stage);
    return false;
}

bool beginOutputTransaction(const QString& projectRoot,
                            const QString& nodeName,
                            const QStringList& expectedFinalPaths,
                            const QStringList& inputPaths,
                            OutputTransaction& transaction,
                            QString* errorMessage)
{
    transaction = OutputTransaction();
    const QDir root(projectRoot);
    if (!root.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project output root does not exist: %1").arg(projectRoot);
        return false;
    }
    if (expectedFinalPaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No expected output files were prepared for node: %1").arg(nodeName);
        return false;
    }
    QString targetReason;
    if (!isDirectProjectChild(root.absolutePath(), nodeName, &targetReason)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid project output transaction target: %1. root=%2, node=%3")
                .arg(targetReason)
                .arg(root.absolutePath())
                .arg(nodeName);
        }
        return false;
    }

    const QString transactionDirectory = transactionDirectoryPath(root.absolutePath());
    if (!QDir().mkpath(transactionDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot create transaction directory: %1").arg(transactionDirectory);
        return false;
    }

    transaction.projectRoot = root.absolutePath();
    transaction.nodeName = nodeName;
    transaction.journalPath = QDir(transactionDirectory).absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (QFileInfo::exists(transaction.journalPath)) {
        QString recoveryError;
        if (!recoverOutputTransaction(root.absolutePath(), nodeName, &recoveryError)) {
            if (errorMessage) *errorMessage = recoveryError;
            return false;
        }
        QFile journal(transaction.journalPath);
        if (!journal.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot inspect unresolved output transaction: %1").arg(transaction.journalPath);
            return false;
        }
        QJsonObject previous = QJsonDocument::fromJson(journal.readAll()).object();
        journal.close();
        const QString previousStage = previous.value(QStringLiteral("stage")).toString();
        if (previousStage != QStringLiteral("Completed") && previousStage != QStringLiteral("Failed")) {
            if (errorMessage) *errorMessage = QStringLiteral("Unresolved output transaction blocks a new run: %1").arg(transaction.journalPath);
            return false;
        }
        if (previousStage == QStringLiteral("Completed")) {
            if (!journalNamesAreSafe(root, nodeName, previous)) {
                if (errorMessage) *errorMessage = QStringLiteral("Previous committed output transaction record has unsafe paths.");
                return false;
            }
            previous.remove(QStringLiteral("previousCommittedJournal"));
            transaction.previousCommittedJournal = previous;
        }
    }

    QSet<QString> uniqueNames;
    for (const QString& expectedPath : expectedFinalPaths) {
        const QFileInfo info(expectedPath);
        if (QDir::cleanPath(info.absolutePath()).compare(
                QDir::cleanPath(root.absoluteFilePath(nodeName)), Qt::CaseInsensitive) != 0 ||
            info.fileName().isEmpty() || uniqueNames.contains(info.fileName())) {
            if (errorMessage) *errorMessage = QStringLiteral("Expected output is not a direct child of the node output directory: %1").arg(expectedPath);
            return false;
        }
        uniqueNames.insert(info.fileName());
        transaction.expectedFileNames.append(info.fileName());
    }

    transaction.inputPaths = inputPaths;
    transaction.inputFingerprints = fingerprintInputs(inputPaths);
    transaction.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    transaction.stagingName = QStringLiteral(".%1.staging-%2").arg(nodeName, transaction.runId);
    transaction.backupName = QStringLiteral(".%1.backup-%2").arg(nodeName, transaction.runId);
    transaction.hasPreviousFinal = QFileInfo(root.absoluteFilePath(nodeName)).exists();
    if (transaction.hasPreviousFinal) {
        QFile previousManifest(root.absoluteFilePath(nodeName + "/" + QString::fromLatin1(kOutputManifestFile)));
        if (previousManifest.open(QIODevice::ReadOnly)) {
            transaction.previousFinalManifest = QJsonDocument::fromJson(previousManifest.readAll()).object();
        }
    }
    if (!isDirectProjectChild(root.absolutePath(), transaction.stagingName) ||
        !isDirectProjectChild(root.absolutePath(), transaction.backupName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot prepare a safe staging directory for output transaction.");
        return false;
    }
    if (!setTransactionStage(transaction, OutputTransaction::Stage::StagingCreatePrepared, errorMessage)) {
        return false;
    }
    if (!root.mkdir(transaction.stagingName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot create staging directory for output transaction.");
        return false;
    }

    return setTransactionStage(transaction, OutputTransaction::Stage::StagingPrepared, errorMessage);
}

bool validateStagedOutputTransaction(OutputTransaction& transaction, QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::StagingPrepared) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction is not in staging state.");
        return false;
    }

    const QDir staging(QDir(transaction.projectRoot).absoluteFilePath(transaction.stagingName));
    if (!fingerprintsMatch(transaction.inputFingerprints, transaction.inputPaths)) {
        if (errorMessage) *errorMessage = QStringLiteral("Input files changed while the output transaction was running.");
        return false;
    }

    QSet<QString> expectedNames;
    for (const QString& name : transaction.expectedFileNames) expectedNames.insert(name);
    const QFileInfoList stagedEntries = staging.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo& entry : stagedEntries) {
        if (entry.isDir() || !expectedNames.contains(entry.fileName())) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Staged output contains an artifact outside the expected manifest: %1")
                    .arg(entry.absoluteFilePath());
            }
            return false;
        }
    }

    QJsonArray files;
    for (const QString& name : transaction.expectedFileNames) {
        const QFileInfo info(staging.absoluteFilePath(name));
        if (!validateOutputFile(info, errorMessage)) return false;
        QJsonObject file;
        file.insert(QStringLiteral("name"), name);
        file.insert(QStringLiteral("size"), static_cast<double>(info.size()));
        file.insert(QStringLiteral("modifiedMs"), static_cast<double>(info.lastModified().toMSecsSinceEpoch()));
        files.append(file);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("runId"), transaction.runId);
    manifest.insert(QStringLiteral("nodeName"), transaction.nodeName);
    manifest.insert(QStringLiteral("inputs"), fingerprintInputs(transaction.inputPaths));
    manifest.insert(QStringLiteral("outputs"), files);
    if (!writeJsonAtomically(staging.absoluteFilePath(QString::fromLatin1(kOutputManifestFile)), manifest, errorMessage)) {
        return false;
    }
    return setTransactionStage(transaction, OutputTransaction::Stage::StagingValidated, errorMessage);
}

bool validateStagedH5Datasets(const OutputTransaction& transaction,
                              const QStringList& requiredDatasets,
                              QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::StagingValidated) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 output validation requires a validated staging transaction.");
        return false;
    }
    if (requiredDatasets.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No required H5 datasets were provided for staged output validation.");
        return false;
    }

    const QDir staging(QDir(transaction.projectRoot).absoluteFilePath(transaction.stagingName));
    for (const QString& name : transaction.expectedFileNames) {
        const QString h5Path = staging.absoluteFilePath(name);
        for (const QString& dataset : requiredDatasets) {
            if (dataset.isEmpty()) {
                if (errorMessage) *errorMessage = QStringLiteral("Staged H5 validation contains an empty dataset name.");
                return false;
            }
            cv::Mat value;
            QString readError;
            if (!readMatFromH5(h5Path, dataset, value, -1, &readError) || value.empty()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Staged H5 output is missing required dataset '%1': %2 (%3)")
                        .arg(dataset, h5Path, readError);
                }
                return false;
            }
        }
    }
    return true;
}

bool promoteOutputTransaction(OutputTransaction& transaction, QStringList& finalPaths, QString* errorMessage)
{
    finalPaths.clear();
    if (transaction.stage != OutputTransaction::Stage::StagingValidated) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction has not passed staging validation.");
        return false;
    }

    QDir root(transaction.projectRoot);
    if (transaction.hasPreviousFinal) {
        if (!setTransactionStage(transaction, OutputTransaction::Stage::BackupMovePrepared, errorMessage) ||
            !root.rename(transaction.nodeName, transaction.backupName)) {
            if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Cannot move previous output directory to backup.");
            return false;
        }
        if (!setTransactionStage(transaction, OutputTransaction::Stage::BackupMoved, errorMessage)) return false;
    }

    if (!setTransactionStage(transaction, OutputTransaction::Stage::PromotionPrepared, errorMessage) ||
        !root.rename(transaction.stagingName, transaction.nodeName)) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Cannot promote staged output directory.");
        return false;
    }
    if (!setTransactionStage(transaction, OutputTransaction::Stage::FinalPromoted, errorMessage)) return false;

    const QDir finalDirectory(root.absoluteFilePath(transaction.nodeName));
    for (const QString& name : transaction.expectedFileNames) {
        const QFileInfo info(finalDirectory.absoluteFilePath(name));
        if (!validateOutputFile(info, errorMessage)) return false;
        finalPaths.append(info.absoluteFilePath());
    }
    return true;
}

bool prepareOutputTransactionMetadataCommit(OutputTransaction& transaction,
                                            XMLFile* xml,
                                            const QString& xmlPath,
                                            QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::FinalPromoted) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot prepare metadata before output promotion.");
        return false;
    }

    const QFileInfo xmlInfo(xmlPath);
    const QDir root(transaction.projectRoot);
    if (!xml || !xmlInfo.isFile() ||
        QDir::cleanPath(xmlInfo.absolutePath()).compare(QDir::cleanPath(root.absolutePath()), Qt::CaseInsensitive) != 0 ||
        !isDirectProjectChild(root.absolutePath(), xmlInfo.fileName())) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML is not a direct project-root file: %1").arg(xmlPath);
        return false;
    }

    transaction.metadataXmlName = xmlInfo.fileName();
    transaction.metadataBackupName = QStringLiteral(".%1.metadata-backup-%2.xml")
        .arg(transaction.nodeName, transaction.runId);
    transaction.metadataBackupReady = false;
    if (!setTransactionStage(transaction, OutputTransaction::Stage::MetadataCommitPrepared, errorMessage)) {
        return false;
    }

    const QString backupPath = metadataBackupPath(transaction);
    if (!saveProjectXmlAtomically(xml, backupPath, errorMessage)) {
        return false;
    }
    transaction.metadataBackupReady = true;
    return persistTransaction(transaction, errorMessage);
}

bool markOutputTransactionMetadataCommitted(OutputTransaction& transaction, QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::MetadataCommitPrepared ||
        !transaction.metadataBackupReady || !QFileInfo(metadataBackupPath(transaction)).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit metadata without a prepared XML backup.");
        return false;
    }
    if (!setTransactionStage(transaction, OutputTransaction::Stage::MetadataCommitted, errorMessage)) return false;

    if (!QFile::remove(metadataBackupPath(transaction))) {
        InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction XML backup: %1")
            .arg(transaction.metadataBackupName));
    }

    QDir root(transaction.projectRoot);
    if (transaction.hasPreviousFinal && QDir(root.absoluteFilePath(transaction.backupName)).exists() &&
        !QDir(root.absoluteFilePath(transaction.backupName)).removeRecursively()) {
        transaction.backupCleanupDeferred = true;
        InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction backup: %1").arg(transaction.backupName));
        transaction.previousCommittedJournal = QJsonObject();
        return setTransactionStage(transaction, OutputTransaction::Stage::Completed, errorMessage);
    }
    transaction.previousCommittedJournal = QJsonObject();
    return setTransactionStage(transaction, OutputTransaction::Stage::Completed, errorMessage);
}

void abandonOutputTransaction(OutputTransaction& transaction, const QString& reason, XMLFile* xml)
{
    if (transaction.stage == OutputTransaction::Stage::Inactive || transaction.projectRoot.isEmpty()) return;
    QDir root(transaction.projectRoot);
    const QString finalPath = root.absoluteFilePath(transaction.nodeName);
    const QString stagingPath = root.absoluteFilePath(transaction.stagingName);
    const QString backupPath = root.absoluteFilePath(transaction.backupName);
    bool rollbackOk = true;

    if (transaction.stage == OutputTransaction::Stage::StagingCreatePrepared) {
        if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
            rollbackOk = false;
        }
        if (rollbackOk && !transaction.previousCommittedJournal.isEmpty()) {
            QString restoreError;
            rollbackOk = writeJsonAtomically(transaction.journalPath,
                                              transaction.previousCommittedJournal,
                                              &restoreError);
            if (!rollbackOk) {
                InSARLogManager::LogError("NodeUtils", QString("Cannot restore previous committed output journal: %1")
                    .arg(restoreError));
            }
        }
        if (rollbackOk && transaction.previousCommittedJournal.isEmpty()) {
            transaction.stage = OutputTransaction::Stage::Failed;
            QString ignored;
            persistTransaction(transaction, &ignored);
        }
        if (!reason.isEmpty()) InSARLogManager::LogWarning("NodeUtils", QString("Output transaction abandoned: %1").arg(reason));
        return;
    }

    if (transaction.stage == OutputTransaction::Stage::MetadataCommitPrepared &&
        !restoreMetadataBackup(transaction, xml, nullptr)) {
        rollbackOk = false;
    }

    // A post-promotion metadata failure must not silently publish the new
    // directory. Restore the previous final when available; otherwise remove
    // the uncommitted candidate. Metadata preparation has already restored
    // the previous XML before this filesystem rollback is attempted.
    if (rollbackOk && (transaction.stage == OutputTransaction::Stage::FinalPromoted ||
                       transaction.stage == OutputTransaction::Stage::MetadataCommitPrepared)) {
        if (transaction.hasPreviousFinal && QDir(backupPath).exists()) {
            if (QDir(finalPath).exists() && !root.rename(transaction.nodeName, transaction.stagingName)) {
                rollbackOk = false;
            }
            if (rollbackOk && !root.rename(transaction.backupName, transaction.nodeName)) {
                rollbackOk = false;
            }
        } else if (!transaction.hasPreviousFinal && QDir(finalPath).exists()) {
            rollbackOk = QDir(finalPath).removeRecursively();
        }
    } else if ((transaction.stage == OutputTransaction::Stage::BackupMoved ||
                transaction.stage == OutputTransaction::Stage::PromotionPrepared) &&
               transaction.hasPreviousFinal && !QDir(finalPath).exists() && QDir(backupPath).exists()) {
        rollbackOk = root.rename(transaction.backupName, transaction.nodeName);
    }

    if (transaction.stage != OutputTransaction::Stage::MetadataCommitted &&
        transaction.stage != OutputTransaction::Stage::Completed) {
        if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
            rollbackOk = false;
        }
        if (rollbackOk) {
            if (!transaction.metadataBackupName.isEmpty() &&
                QFileInfo(metadataBackupPath(transaction)).exists() &&
                !QFile::remove(metadataBackupPath(transaction))) {
                InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction XML backup: %1")
                    .arg(transaction.metadataBackupName));
            }
            if (!transaction.previousCommittedJournal.isEmpty()) {
                QString restoreError;
                rollbackOk = writeJsonAtomically(transaction.journalPath,
                                                  transaction.previousCommittedJournal,
                                                  &restoreError);
                if (!rollbackOk) {
                    InSARLogManager::LogError("NodeUtils", QString("Cannot restore previous committed output journal: %1")
                        .arg(restoreError));
                }
            } else {
                transaction.stage = OutputTransaction::Stage::Failed;
                QString ignored;
                persistTransaction(transaction, &ignored);
            }
        } else {
            InSARLogManager::LogError("NodeUtils", QString("Output transaction rollback requires recovery: %1").arg(transaction.journalPath));
        }
        if (!reason.isEmpty()) InSARLogManager::LogWarning("NodeUtils", QString("Output transaction abandoned: %1").arg(reason));
    }
}

bool loadCommittedOutputManifest(const QString& projectRoot,
                                 const QString& nodeName,
                                 QStringList& outputPaths,
                                 QString* errorMessage)
{
    outputPaths.clear();
    const QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) return false;
    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath())).absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (!QFileInfo::exists(journalPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("No committed output transaction record exists.");
        return false;
    }
    QFile journal(journalPath);
    if (!journal.open(QIODevice::ReadOnly)) return false;
    const QJsonObject journalObject = QJsonDocument::fromJson(journal.readAll()).object();
    if (!journalNamesAreSafe(root, nodeName, journalObject)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction record has unsafe paths.");
        return false;
    }
    const QString stage = journalObject.value(QStringLiteral("stage")).toString();
    if (stage != QStringLiteral("MetadataCommitted") && stage != QStringLiteral("Completed")) {
        QString recoveryError;
        const bool recovered = recoverOutputTransaction(root.absolutePath(), nodeName, &recoveryError);
        if (recovered) {
            QFile recoveredJournal(journalPath);
            if (recoveredJournal.open(QIODevice::ReadOnly)) {
                const QJsonObject recoveredObject = QJsonDocument::fromJson(recoveredJournal.readAll()).object();
                const QString recoveredStage = recoveredObject.value(QStringLiteral("stage")).toString();
                if (recoveredStage == QStringLiteral("MetadataCommitted") ||
                    recoveredStage == QStringLiteral("Completed")) {
                    return loadCommittedOutputManifest(projectRoot, nodeName, outputPaths, errorMessage);
                }
            }
        }
        if (errorMessage) {
            *errorMessage = recovered
                ? QStringLiteral("Interrupted output transaction was cleaned up; output remains invalid until rerun.")
                : recoveryError;
        }
        return false;
    }
    if (journalObject.value(QStringLiteral("nodeName")).toString() != nodeName ||
        journalObject.value(QStringLiteral("runId")).toString().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction record is invalid.");
        return false;
    }

    QFile manifest(QDir(root.absoluteFilePath(nodeName)).absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
    if (!manifest.open(QIODevice::ReadOnly)) return false;
    const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
    if (object.value(QStringLiteral("nodeName")).toString() != nodeName ||
        object.value(QStringLiteral("runId")).toString() != journalObject.value(QStringLiteral("runId")).toString()) {
        if (errorMessage) *errorMessage = QStringLiteral("Output manifest does not match the committed transaction.");
        return false;
    }
    const QJsonArray outputs = object.value(QStringLiteral("outputs")).toArray();
    QSet<QString> expectedNames;
    for (const QJsonValue& value : journalObject.value(QStringLiteral("expectedFiles")).toArray()) {
        const QString name = value.toString();
        if (name.isEmpty() || expectedNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction has an invalid expected file list.");
            return false;
        }
        expectedNames.insert(name);
    }
    if (expectedNames.isEmpty() || outputs.size() != expectedNames.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("Output manifest file count does not match the committed transaction.");
        return false;
    }
    QSet<QString> actualNames;
    for (const QJsonValue& value : outputs) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (name.isEmpty() || !expectedNames.contains(name) || actualNames.contains(name)) return false;
        actualNames.insert(name);
        const QFileInfo info(root.absoluteFilePath(nodeName + "/" + name));
        if (!validateOutputFile(info, errorMessage)) return false;
        outputPaths.append(info.absoluteFilePath());
    }
    return !outputPaths.isEmpty();
}

bool workerOutputsMatchManifest(const QStringList& manifestPaths,
                                const QStringList& workerPaths,
                                QString* errorMessage)
{
    if (manifestPaths.size() != workerPaths.size() || manifestPaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Worker output count does not match the transaction manifest.");
        return false;
    }

    QSet<QString> expectedNames;
    for (const QString& path : manifestPaths) {
        const QString name = QFileInfo(path).fileName();
        if (name.isEmpty() || expectedNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Transaction manifest has duplicate or invalid output names.");
            return false;
        }
        expectedNames.insert(name);
    }

    QSet<QString> workerNames;
    for (const QString& path : workerPaths) {
        const QString name = QFileInfo(path).fileName();
        if (name.isEmpty() || !expectedNames.contains(name) || workerNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Worker output is missing from the transaction manifest: %1").arg(path);
            return false;
        }
        workerNames.insert(name);
    }
    return workerNames == expectedNames;
}

bool saveProjectXmlAtomically(XMLFile* xml, const QString& xmlPath, QString* errorMessage)
{
    if (!xml || xmlPath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML context is unavailable.");
        return false;
    }

    const QFileInfo targetInfo(xmlPath);
    const QDir targetDirectory(targetInfo.absolutePath());
    if (!targetDirectory.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML directory does not exist: %1").arg(targetInfo.absolutePath());
        return false;
    }

    const QString temporaryPath = targetDirectory.absoluteFilePath(
        QStringLiteral(".%1.transaction-%2.tmp").arg(targetInfo.fileName(),
            QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QByteArray temporaryName = QDir::toNativeSeparators(temporaryPath).toLocal8Bit();
    if (xml->XMLFile_save(temporaryName.constData()) < 0 ||
        !QFileInfo(temporaryPath).isFile() || QFileInfo(temporaryPath).size() <= 0) {
        QFile::remove(temporaryPath);
        xml->XMLFile_load(QDir::toNativeSeparators(xmlPath).toLocal8Bit().constData());
        if (errorMessage) *errorMessage = QStringLiteral("Failed to write temporary project XML: %1").arg(temporaryPath);
        return false;
    }

#ifdef Q_OS_WIN
    if (!::MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryPath.utf16()),
                       reinterpret_cast<LPCWSTR>(xmlPath.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = ::GetLastError();
        QFile::remove(temporaryPath);
        xml->XMLFile_load(QDir::toNativeSeparators(xmlPath).toLocal8Bit().constData());
        if (errorMessage) *errorMessage = QStringLiteral("Failed to replace project XML (%1): Win32 error %2")
            .arg(xmlPath).arg(static_cast<qulonglong>(error));
        return false;
    }
#else
    QFile::remove(xmlPath);
    if (!QFile::rename(temporaryPath, xmlPath)) {
        QFile::remove(temporaryPath);
        xml->XMLFile_load(QDir::toNativeSeparators(xmlPath).toLocal8Bit().constData());
        if (errorMessage) *errorMessage = QStringLiteral("Failed to replace project XML: %1").arg(xmlPath);
        return false;
    }
#endif
    return true;
}

bool removeOutputFiles(const QStringList& filePaths)
{
    bool allSuccess = true;
    for (const QString& path : filePaths) {
        if (path.isEmpty()) continue;
        if (QFile::exists(path)) {
            if (!QFile::remove(path)) {
                InSARLogManager::LogWarning("NodeUtils", QString("无法物理删除旧文件：%1，文件可能正被占用。").arg(path));
                allSuccess = false;
            }
        }

        if (path.endsWith(".h5", Qt::CaseInsensitive)) {
            QString jpgPath = path;
            jpgPath.chop(3);
            jpgPath += ".jpg";
            if (QFile::exists(jpgPath)) {
                if (!QFile::remove(jpgPath)) {
                    InSARLogManager::LogWarning("NodeUtils", QString("无法物理删除旧预览图：%1，文件可能正被占用。").arg(jpgPath));
                    allSuccess = false;
                }
            }
        }
    }
    return allSuccess;
}

bool isJpgPreviewCurrent(const QStringList& inputPaths, const QString& jpgPath)
{
    QFileInfo jpgInfo(jpgPath);
    if (inputPaths.isEmpty() || !jpgInfo.exists() || jpgInfo.size() <= 0) {
        return false;
    }

    for (const QString& inputPath : inputPaths) {
        QFileInfo inputInfo(inputPath);
        if (inputPath.isEmpty() || !inputInfo.exists() ||
            jpgInfo.lastModified() < inputInfo.lastModified()) {
            return false;
        }
    }
    return true;
}

bool isJpgPreviewCurrent(const QString& h5Path, const QString& jpgPath)
{
    return isJpgPreviewCurrent(QStringList() << h5Path, jpgPath);
}

bool replaceJpgPreviewAtomically(const QString& temporaryJpgPath, const QString& jpgPath)
{
    // JPGs are written by OpenCV/Utils. Do not validate them through Qt's
    // optional image-format plugins, which are not deployed in every runtime.
    if (!isCompleteJpegFile(temporaryJpgPath)) {
        return false;
    }

#ifdef Q_OS_WIN
    return ::MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryJpgPath.utf16()),
                         reinterpret_cast<LPCWSTR>(jpgPath.utf16()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (QFile::exists(jpgPath) && !QFile::remove(jpgPath)) {
        return false;
    }
    return QFile::rename(temporaryJpgPath, jpgPath);
#endif
}

static bool generateJpgPreviewFromH5Direct(const QString& h5Path, const QString& jpgPath,
                                            const QString& type, std::function<void(int, int)> cb);

bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type)
{
    return generateJpgPreviewFromH5WithProgress(h5Path, jpgPath, type, nullptr);
}

bool generateJpgPreviewFromH5WithProgress(const QString& h5Path, const QString& jpgPath, const QString& type, std::function<void(int, int)> cb)
{
    if (h5Path.isEmpty() || jpgPath.isEmpty()) {
        InSARLogManager::LogWarning("NodeUtils", "JPG preview generation skipped because the H5 or JPG path is empty.");
        return false;
    }

    const QFileInfo jpgInfo(jpgPath);
    const QString tempPath = jpgInfo.absolutePath() + "/." + jpgInfo.completeBaseName() +
        "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
    QFile::remove(tempPath);

    if (!generateJpgPreviewFromH5Direct(h5Path, tempPath, type, cb)) {
        InSARLogManager::LogWarning("NodeUtils", QString("JPG preview rendering failed: %1").arg(h5Path));
        QFile::remove(tempPath);
        return false;
    }

    // Do not publish a preview if its H5 changed while rendering.
    if (!isJpgPreviewCurrent(h5Path, tempPath)) {
        InSARLogManager::LogWarning("NodeUtils", QString("JPG preview discarded because its source changed: %1").arg(h5Path));
        QFile::remove(tempPath);
        return false;
    }

    if (!replaceJpgPreviewAtomically(tempPath, jpgPath)) {
        InSARLogManager::LogWarning("NodeUtils", QString("JPG preview validation or atomic publication failed: %1").arg(jpgPath));
        QFile::remove(tempPath);
        return false;
    }
    return true;
}

static bool generateJpgPreviewFromH5Direct(const QString& h5Path, const QString& jpgPath, const QString& type, std::function<void(int, int)> cb)
{
    Hdf5Locker locker;
    if (h5Path.isEmpty() || jpgPath.isEmpty())
        return false;

    if (QFile::exists(jpgPath) && !QFile::remove(jpgPath))
        return false;

    Utils util;
    FormatConversion FC;

    // 局部 Lambda 帮助函数：手动归一化与保存相位 JPG（用于 savephase 失败时的备用逻辑）
    auto savePhaseFallback = [&](const cv::Mat& mat_to_save, const QString& type_str) -> int {
        cv::Mat phase_normalized;
        if (type_str == "coherence")
        {
            phase_normalized = mat_to_save * 255.0;
        }
        else if (type_str == "dem")
        {
            double minVal, maxVal;
            cv::minMaxLoc(mat_to_save, &minVal, &maxVal);
            if (maxVal - minVal > 1e-6)
            {
                phase_normalized = (mat_to_save - minVal) * (255.0 / (maxVal - minVal));
            }
            else
            {
                phase_normalized = cv::Mat::zeros(mat_to_save.size(), CV_64F);
            }
        }
        else
        {
            phase_normalized = (mat_to_save + 3.141592653589793) * (255.0 / (2.0 * 3.141592653589793));
        }
        
        phase_normalized.convertTo(phase_normalized, CV_8U);
        
        cv::Mat color_image;
        if (type_str == "coherence")
        {
            color_image = phase_normalized;
        }
        else
        {
            cv::applyColorMap(phase_normalized, color_image, cv::COLORMAP_JET);
        }
        
        bool success_write = cv::imwrite(jpgPath.toStdString(), color_image);
        return success_write ? 0 : -1;
    };

    if (type == "complex")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toLocal8Bit().constData(), "s_re", &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        if (down_sample_times <= 1)
        {
            ComplexMat SLC64;
            if (FC.read_slc_from_h5(h5Path.toLocal8Bit().constData(), SLC64) != 0)
                return false;
                
            util.saveSLC(jpgPath.toLocal8Bit().constData(), 65, SLC64);
            if (cb) cb(rows, rows);
            return true;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        ComplexMat downsampled_SLC(dst_rows, dst_cols);

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_re, block_im;

            if (FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_re", r, 0, rows_to_read, cols, block_re) != 0 ||
                FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_im", r, 0, rows_to_read, cols, block_im) != 0)
            {
                return false;
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_re, down_im;
                cv::resize(block_re, down_re, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);
                cv::resize(block_im, down_im, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_re.copyTo(downsampled_SLC.re(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                    down_im.copyTo(downsampled_SLC.im(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        util.saveSLC(jpgPath.toLocal8Bit().constData(), 65, downsampled_SLC);
        return true;
    }
    else if (type == "phase" || type == "coherence" || type == "dem")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toStdString().c_str(), type.toStdString().c_str(), &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        cv::Mat phase;
        int ret = -1;

        if (down_sample_times <= 1)
        {
            if (FC.read_array_from_h5(h5Path.toStdString().c_str(), type.toStdString().c_str(), phase) != 0)
                return false;
                
            if (phase.type() != CV_64F)
            {
                phase.convertTo(phase, CV_64F);
            }
            phase = phase.clone();
                
            if (type == "phase")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", phase);
            }
            else if (type == "coherence")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "gray", phase);
            }
            else if (type == "dem")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", phase);
            }
            
            if (ret != 0)
            {
                ret = savePhaseFallback(phase, type);
            }
            if (cb) cb(rows, rows);
            return ret == 0;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_phase(dst_rows, dst_cols, CV_64F);

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_phase;

            if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), type.toStdString().c_str(), r, 0, rows_to_read, cols, block_phase) != 0)
                return false;

            if (block_phase.type() != CV_64F)
            {
                block_phase.convertTo(block_phase, CV_64F);
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_block;
                cv::resize(block_phase, down_block, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_block.copyTo(downsampled_phase(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        if (type == "phase")
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "jet", downsampled_phase);
        }
        else if (type == "coherence")
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "gray", downsampled_phase);
        }
        else if (type == "dem")
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "jet", downsampled_phase);
        }

        if (ret != 0)
        {
            ret = savePhaseFallback(downsampled_phase, type);
        }
        return ret == 0;
    }
    else if (type == "amplitude")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toStdString().c_str(), "amplitude", &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        cv::Mat phase;
        int ret = -1;

        if (down_sample_times <= 1)
        {
            if (FC.read_array_from_h5(h5Path.toStdString().c_str(), "amplitude", phase) != 0)
                return false;
                
            if (phase.type() != CV_32F)
            {
                phase.convertTo(phase, CV_32F);
            }
            phase = phase.clone();
            
            ret = util.saveAmplitude(jpgPath.toStdString().c_str(), phase);
            if (cb) cb(rows, rows);
            return ret == 0;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_amplitude(dst_rows, dst_cols, CV_32F);

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_amp;

            if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "amplitude", r, 0, rows_to_read, cols, block_amp) != 0)
                return false;

            if (block_amp.type() != CV_32F)
            {
                block_amp.convertTo(block_amp, CV_32F);
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_block;
                cv::resize(block_amp, down_block, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_block.copyTo(downsampled_amplitude(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        ret = util.saveAmplitude(jpgPath.toStdString().c_str(), downsampled_amplitude);
        return ret == 0;
    }
    else if (type == "SBAS")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toStdString().c_str(), "defomation_velocity", &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        cv::Mat defomation_velocity, mask;
        int ret_vel = -1, ret_mask = -1;

        if (down_sample_times <= 1)
        {
            ret_vel = FC.read_array_from_h5(h5Path.toStdString().c_str(), "defomation_velocity", defomation_velocity);
            if (ret_vel != 0)
                return false;
                
            ret_mask = FC.read_array_from_h5(h5Path.toStdString().c_str(), "mask", mask);
            
            if (defomation_velocity.type() != CV_64F)
            {
                defomation_velocity.convertTo(defomation_velocity, CV_64F);
            }
            defomation_velocity = defomation_velocity.clone();
            
            int ret = -1;
            if (ret_mask == 0)
            {
                ret = util.savephase_white(jpgPath.toStdString().c_str(), "jet", defomation_velocity, mask);
            }
            else
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", defomation_velocity);
            }
            if (cb) cb(rows, rows);
            return ret == 0;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_vel(dst_rows, dst_cols, CV_64F);
        cv::Mat downsampled_mask;

        int mask_rows = 0, mask_cols = 0;
        bool has_mask = (FC.get_dataset_dims(h5Path.toStdString().c_str(), "mask", &mask_rows, &mask_cols) == 0);
        if (has_mask)
        {
            downsampled_mask.create(dst_rows, dst_cols, CV_32S);
        }

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_vel;

            if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "defomation_velocity", r, 0, rows_to_read, cols, block_vel) != 0)
                return false;

            if (block_vel.type() != CV_64F)
            {
                block_vel.convertTo(block_vel, CV_64F);
            }

            cv::Mat block_mask;
            if (has_mask)
            {
                if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mask", r, 0, rows_to_read, cols, block_mask) != 0)
                {
                    has_mask = false;
                }
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_vel;
                cv::resize(block_vel, down_vel, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_vel.copyTo(downsampled_vel(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }

                if (has_mask && !block_mask.empty())
                {
                    cv::Mat down_mask;
                    cv::resize(block_mask, down_mask, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_NEAREST);
                    if (r_dst + block_dst_rows <= dst_rows)
                    {
                        down_mask.copyTo(downsampled_mask(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                    }
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        int ret = -1;
        if (has_mask && !downsampled_mask.empty())
        {
            ret = util.savephase_white(jpgPath.toStdString().c_str(), "jet", downsampled_vel, downsampled_mask);
        }
        else
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "jet", downsampled_vel);
        }
        return ret == 0;
    }
    return false;
}

QStandardItem* findOrCreateProjectNode(
    QStandardItem* project,
    const QString& nodeName,
    const QString& rankType,
    const QString& iconPath,
    bool* created)
{
    if (!project) return nullptr;

    // 1. 查找是否已存在相同名称和 Rank 的节点
    for (int i = 0; i < project->rowCount(); ++i) {
        if (project->child(i, 0)->text() == nodeName &&
            project->child(i, 1) && project->child(i, 1)->text() == rankType) {
            if (created) *created = false;
            return project->child(i, 0);
        }
    }

    if (created) *created = true;

    // 2. 统一定义项目树所有阶段 of Rank 排序顺序
    static const QStringList RANK_ORDER = {
        // === 1. 复数图像数据阶段 ===
        "complex-0.0",     // 原始导入图像
        "complex-1.0",     // 裁剪后图像 / 去爆
        "complex-2.0",     // 配准后图像
        "complex-3.0",     // 去斜率图像
        
        // === 2. 相位数据阶段 ===
        "phase-1.0",       // 干涉相位图
        "phase-1.1",       // 【地理编码】干涉相位图
        
        // === 3. 相干性数据阶段 ===
        "coherence-1.0",   // 相干系数图
        "coherence-1.1",   // 【地理编码】相干系数图
        
        // === 4. 滤波与解缠相位阶段 ===
        "phase-2.0",       // 滤波相位图
        "phase-2.1",       // 【地理编码】滤波相位图
        "phase-3.0",       // 解缠相位图
        "phase-3.1",       // 【地理编码】解缠相位图
        
        // === 5. 高程与最终产品阶段 ===
        "dem-1.0",         // 雷达坐标系 DEM
        "dem-1.1",         // 【地理编码】地理坐标系 DEM
        "SBAS-1.0",        // 时序形变速率
        "SBAS-1.1"         // 【地理编码】时序形变速率
    };

    int targetIdx = RANK_ORDER.indexOf(rankType);
    if (targetIdx == -1) targetIdx = 999; // 未知 rank 默认放最后

    // 3. 寻找正确的排序插入位置
    int insert = 0;
    for (; insert < project->rowCount(); ++insert) {
        QStandardItem* rankCol = project->child(insert, 1);
        QString childRank = rankCol ? rankCol->text() : "";
        int childIdx = RANK_ORDER.indexOf(childRank);
        if (childIdx == -1) childIdx = 999;

        if (childIdx <= targetIdx) {
            continue;
        } else {
            break;
        }
    }

    // 4. 创建并插入节点
    QStandardItem* node = new QStandardItem(nodeName);
    QString finalIcon = iconPath.isEmpty() ? FOLDER_ICON : iconPath;
    node->setIcon(QIcon(finalIcon));
    project->insertRow(insert, node);

    QStandardItem* rankItem = new QStandardItem(rankType);
    project->setChild(insert, 1, rankItem);

    return node;
}

QStandardItem* findOrCreateChildItem(
    QStandardItem* parent,
    const QString& childName,
    const QString& tooltip,
    const QString& h5Path,
    const QString& iconPath,
    bool* created)
{
    if (!parent) return nullptr;

    // 1. 查找是否已存在该子项
    for (int i = 0; i < parent->rowCount(); ++i) {
        if (parent->child(i, 0)->text() == childName) {
            if (created) *created = false;
            return parent->child(i, 0);
        }
    }

    if (created) *created = true;

    // 2. 创建子项
    QStandardItem* item = new QStandardItem(childName);
    item->setToolTip(tooltip);
    
    QString finalIcon = iconPath.isEmpty() ? IMAGEDATA_ICON : iconPath;
    item->setIcon(QIcon(finalIcon));

    // 3. 插入子项并绑定路径到第二列
    parent->appendRow(item);
    
    QStandardItem* pathItem = new QStandardItem(h5Path);
    parent->setChild(parent->rowCount() - 1, 1, pathItem);

    return item;
}

// ---------------------------------------------------------------------
// 读取 cv::Mat 矩阵数据
// ---------------------------------------------------------------------
bool readMatFromH5(const QString& filePath,
                   const QString& dataset,
                   cv::Mat& mat,
                   int targetType,
                   QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_array_from_h5(filePath.toStdString().c_str(),
                                   dataset.toStdString().c_str(),
                                   mat);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 数据集 %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }

    if (targetType >= 0 && mat.type() != targetType) {
        mat.convertTo(mat, targetType);
    }
    return true;
}

// ---------------------------------------------------------------------
// 读取标量数据（重载实现，支持 int, double, float, qint64）
// ---------------------------------------------------------------------
bool readScalarFromH5(const QString& filePath, const QString& dataset, int& value, QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_int_from_h5(filePath.toStdString().c_str(),
                                 dataset.toStdString().c_str(),
                                 &value);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 标量(int) %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

bool readScalarFromH5(const QString& filePath, const QString& dataset, double& value, QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_double_from_h5(filePath.toStdString().c_str(),
                                    dataset.toStdString().c_str(),
                                    &value);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 标量(double) %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

bool readScalarFromH5(const QString& filePath, const QString& dataset, float& value, QString* errMsg)
{
    double tmp = 0.0;
    if (!readScalarFromH5(filePath, dataset, tmp, errMsg)) {
        return false;
    }
    value = static_cast<float>(tmp);
    return true;
}

bool readScalarFromH5(const QString& filePath, const QString& dataset, qint64& value, QString* errMsg)
{
    int tmp = 0;
    if (!readScalarFromH5(filePath, dataset, tmp, errMsg)) {
        return false;
    }
    value = static_cast<qint64>(tmp);
    return true;
}

// ---------------------------------------------------------------------
// 读取字符串数据
// ---------------------------------------------------------------------
bool readStringFromH5(const QString& filePath,
                      const QString& dataset,
                      std::string& out,
                      QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_str_from_h5(filePath.toStdString().c_str(),
                                 dataset.toStdString().c_str(),
                                 out);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 字符串 %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// 写入 cv::Mat 矩阵数据
// ---------------------------------------------------------------------
bool writeMatToH5(const QString& filePath,
                  const QString& dataset,
                  const cv::Mat& mat,
                  QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath);
    FormatConversion FC;

    // write_array_to_h5 底层接口接收 cv::Mat&，我们使用 const_cast 去除 const 限制
    int rc = FC.write_array_to_h5(filePath.toStdString().c_str(),
                                  dataset.toStdString().c_str(),
                                  const_cast<cv::Mat&>(mat));
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("写入 H5 数据集 %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// 写入标量数据
// ---------------------------------------------------------------------
bool writeScalarToH5(const QString& filePath, const QString& dataset, int value, QString* errMsg)
{
    cv::Mat tmp = cv::Mat::zeros(1, 1, CV_32SC1);
    tmp.at<int>(0, 0) = value;
    return writeMatToH5(filePath, dataset, tmp, errMsg);
}

bool writeScalarToH5(const QString& filePath, const QString& dataset, double value, QString* errMsg)
{
    cv::Mat tmp = cv::Mat::zeros(1, 1, CV_64FC1);
    tmp.at<double>(0, 0) = value;
    return writeMatToH5(filePath, dataset, tmp, errMsg);
}

QString getGlobalDemPath(IApplicationInterface* iface)
{
    if (!iface) return QString();
    XMLFile* xml = iface->projectXml();
    if (!xml) return QString();
    
    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root) return QString();
    
    TiXmlElement* pnode = nullptr;
    xml->_find_node(root, "globalDemPath", pnode);
    if (pnode && pnode->GetText()) {
        return QString::fromUtf8(pnode->GetText());
    }
    
    // 缺省时返回默认的项目级缓存路径项目目录/.dem_cache
    QString projPath = iface->projectPath();
    if (projPath.isEmpty()) return QString();
    
    QFileInfo fi(projPath);
    QString projectDir = fi.absolutePath();
    return QDir::toNativeSeparators(projectDir + "/.dem_cache");
}

bool setGlobalDemPath(IApplicationInterface* iface, const QString& path, bool askUser)
{
    if (!iface || path.isEmpty()) return false;
    
    XMLFile* xml = iface->projectXml();
    if (!xml) return false;
    
    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root) return false;
    
    QString cleanPath = QDir::toNativeSeparators(path);
    
    // 判断是否已经是该全局路径，避免重复设置
    QString currentGlobal = getGlobalDemPath(iface);
    if (QDir::toNativeSeparators(currentGlobal) == cleanPath) {
        return true;
    }
    
    if (askUser) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            nullptr,
            QStringLiteral("设置全局高程数据"),
            QStringLiteral("是否将该路径应用为本项目的全局默认高程数据？\n\n新路径：%1").arg(cleanPath),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply != QMessageBox::Yes) {
            return false;
        }
    }
    
    TiXmlElement* pnode = nullptr;
    xml->_find_node(root, "globalDemPath", pnode);
    if (!pnode) {
        pnode = new TiXmlElement("globalDemPath");
        root->LinkEndChild(pnode);
    }
    
    pnode->Clear();
    pnode->LinkEndChild(new TiXmlText(cleanPath.toUtf8().constData()));
    
    // 保存项目 XML
    xml->XMLFile_save(iface->projectPath().toStdString().c_str());
    
    // 联动更新整个应用程序中所有已启用（未连线）的 demPathEdit 控件
    foreach (QWidget* widget, QApplication::allWidgets()) {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget);
        if (lineEdit && lineEdit->objectName() == "demPathEdit") {
            if (lineEdit->isEnabled()) {
                lineEdit->setText(cleanPath);
                emit lineEdit->editingFinished();
            }
        }
    }
    
    return true;
}

QString getConfigPath()
{
    return QCoreApplication::applicationDirPath() + "/Config.ini";
}

QString getModelPath(const QString& modelName)
{
    // 优先尝试从可执行文件同级目录 (bin/) 查找
    QString binPath = QCoreApplication::applicationDirPath() + "/" + modelName;
    if (QFileInfo::exists(binPath)) {
        return binPath;
    }
    // 调试回退：如果 bin/ 里没有，从当前工作目录查找
    return QDir::currentPath() + "/" + modelName;
}

void safeThreadWait(QThread* thread, int timeoutMs)
{
    if (!thread) return;
    while (thread->isRunning())
    {
        thread->wait(timeoutMs);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

QString getProjectFilePath(QWidget* widget)
{
    auto* iface = getProjectContext(widget);
    return iface ? iface->projectPath() : QString();
}

QString getProjectDirectory(QWidget* widget)
{
    return projectDirectory(getProjectFilePath(widget));
}

QString projectDirectory(const QString& projectPath)
{
    if (projectPath.isEmpty()) return QString();
    if (projectPath.endsWith(".insar", Qt::CaseInsensitive))
    {
        return QFileInfo(projectPath).absolutePath();
    }
    return projectPath;
}

bool writeDemToTif(const QString& tifPath, const cv::Mat& dem, const double* gt, const char* wkt)
{
    Hdf5Locker locker(tifPath);

    GDALAllRegister();
    GDALDriver* poDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!poDriver) return false;

    int cols = dem.cols;
    int rows = dem.rows;
    GDALDataset* poDstDS = poDriver->Create(tifPath.toLocal8Bit().constData(), cols, rows, 1, GDT_Float32, nullptr);
    if (!poDstDS) return false;

    poDstDS->SetGeoTransform(const_cast<double*>(gt));
    if (wkt) poDstDS->SetProjection(wkt);

    GDALRasterBand* poBand = poDstDS->GetRasterBand(1);
    poBand->SetNoDataValue(-32767.0);
    
    cv::Mat floatDem;
    if (dem.type() != CV_32F) {
        dem.convertTo(floatDem, CV_32F);
    } else {
        floatDem = dem;
    }

    CPLErr err = poBand->RasterIO(GF_Write, 0, 0, cols, rows, floatDem.data, cols, rows, GDT_Float32, 0, 0);
    GDALClose(poDstDS);

    if (err != CE_None) {
        QFile::remove(tifPath);
        return false;
    }

    return true;
}

} // namespace NodeUtils
