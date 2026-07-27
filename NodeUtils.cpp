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
#include <QMessageBox>
#include <QDir>
#include <QLineEdit>
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

void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName)
{
    if (!iface || oldNodeName.isEmpty()) return;

    QStandardItemModel* model = iface->projectModel();
    QString projPath = iface->projectPath();
    QString projName = iface->projectName();

    if (!model || projPath.isEmpty() || projName.isEmpty()) return;

    // projectPath() from ImportNodeBase returns QFileInfo(fullPath).absolutePath()
    // i.e. the directory containing the .Insar file.
    // projName is the .Insar filename (e.g. "myproject.Insar")
    // XML path = projPath + "/" + projName  (but projPath may already be the dir)

    // Find the project item in the model
    QList<QStandardItem*> projItems = model->findItems(projName);
    // If projName is a filename like "test.Insar", also try without extension
    if (projItems.isEmpty()) {
        // Try to find by iterating top-level items
        for (int r = 0; r < model->rowCount(); ++r) {
            QStandardItem* item = model->item(r, 0);
            if (item) {
                projItems.append(item);
            }
        }
    }

    for (QStandardItem* projItem : projItems) {
        // Search for the DataNode child with matching name
        for (int i = projItem->rowCount() - 1; i >= 0; --i) {
            QStandardItem* nodeItem = projItem->child(i, 0);
            if (nodeItem && nodeItem->text() == oldNodeName) {
                projItem->removeRow(i);
            }
        }
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
    xml->XMLFile_save(xmlPath.toStdString().c_str());
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

bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type)
{
    return generateJpgPreviewFromH5WithProgress(h5Path, jpgPath, type, nullptr);
}

bool isJpgPreviewCurrent(const QString& h5Path, const QString& jpgPath)
{
    QFileInfo h5Info(h5Path);
    QFileInfo jpgInfo(jpgPath);
    return h5Info.exists() && jpgInfo.exists() && jpgInfo.size() > 0 &&
        jpgInfo.lastModified() >= h5Info.lastModified();
}

bool generateJpgPreviewFromH5WithProgress(const QString& h5Path, const QString& jpgPath, const QString& type, std::function<void(int, int)> cb)
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
