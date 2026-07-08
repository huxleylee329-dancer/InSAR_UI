#pragma once

#include <QString>
#include <QMutex>

#include <functional>

#include <opencv2/core.hpp>

class QWidget;
class IApplicationInterface;

class QStandardItem;

namespace NodeUtils {

QMutex* getHdf5Mutex();

class Hdf5Locker {
public:
    Hdf5Locker();
    ~Hdf5Locker();
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

} // namespace NodeUtils
