#pragma once

#include <QString>

class QWidget;
class IApplicationInterface;

namespace NodeUtils {

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
void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName);

enum class OverwriteResult {
    NoConflict,
    Overwrite,
    LoadExisting,
    Cancel
};

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
 * @brief 从 H5 科学数据文件中提取幅值并生成 JPG 预览图（自动进行超大图降采样）
 * @param h5Path H5文件路径
 * @param jpgPath 输出JPG路径
 * @param type 数据类型，支持 "complex"（复数SLC）和 "phase"（相位）
 * @return 是否生成成功
 */
bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type = "complex");

} // namespace NodeUtils
