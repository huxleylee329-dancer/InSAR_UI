#ifndef IAPPLICATIONINTERFACE_H
#define IAPPLICATIONINTERFACE_H

#include <QWidget>
#include <QList>
#include <QToolBar>
#include <QString>
#include <QStandardItemModel>

// Forward declaration for XMLFile
class XMLFile;

/**
 * @brief 应用界面抽象接口
 *
 * 所有可切换的应用界面都需要实现这个接口
 */
class IApplicationInterface
{
public:
    virtual ~IApplicationInterface() = default;

    /**
     * @brief 获取界面的中心控件
     * @return 中心控件指针
     */
    virtual QWidget* centralWidget() = 0;

    /**
     * @brief 获取界面的所有工具栏
     * @return 工具栏列表
     */
    virtual QList<QToolBar*> toolBars() = 0;

    /**
     * @brief 激活界面 - 当切换到该界面时调用
     */
    virtual void activate() = 0;

    /**
     * @brief 停用界面 - 当切出该界面时调用
     */
    virtual void deactivate() = 0;

    /**
     * @brief 获取界面唯一ID
     * @return "workspace" 或 "workflow"
     */
    virtual QString id() const = 0;

    /**
     * @brief 获取界面显示名称
     * @return 用于菜单显示的名称
     */
    virtual QString displayName() const = 0;

    /**
     * @brief 设置项目上下文
     * @param model 项目数据模型
     * @param path 项目文件路径
     * @param name 项目名称
     * @param projectXml 项目XML对象指针
     */
    virtual void setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml = nullptr) = 0;

    /**
     * @brief 获取项目数据模型
     * @return 项目模型指针
     */
    virtual QStandardItemModel* projectModel() const = 0;

    /**
     * @brief 获取项目文件路径
     * @return 项目文件路径
     */
    virtual QString projectPath() const = 0;

    /**
     * @brief 获取项目名称
     * @return 项目名称
     */
    virtual QString projectName() const = 0;

    /**
     * @brief 获取项目XML对象
     * @return XMLFile指针
     */
    virtual XMLFile* projectXml() const = 0;

    /**
     * @brief 初始化主题（构造时调用）
     */
    virtual void initTheme() = 0;

    /**
     * @brief 应用主题
     * @param theme 主题名称（"light"、"dark"、"fusion"）
     */
    virtual void setTheme(const QString& theme) = 0;

    /**
     * @brief 刷新项目树视图（导入数据后调用）
     */
    virtual void refreshProjectTree() = 0;

    /**
     * @brief 清空界面内容（关闭项目时调用）
     */
    virtual void clear() = 0;
};

#endif // IAPPLICATIONINTERFACE_H
