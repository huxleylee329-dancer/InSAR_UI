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

/**
 * @brief 从 H5 科学数据文件中提取幅值并生成 JPG 预览图（自动进行超大图降采样）
 * @param h5Path H5文件路径
 * @param jpgPath 输出JPG路径
 * @param type 数据类型，支持 "complex"（复数SLC）和 "phase"（相位）
 * @return 是否生成成功
 */
bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type = "complex");

} // namespace NodeUtils
