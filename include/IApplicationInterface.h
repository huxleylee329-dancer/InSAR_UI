#ifndef IAPPLICATIONINTERFACE_H
#define IAPPLICATIONINTERFACE_H

#include <QWidget>
#include <QList>
#include <QToolBar>
#include <QString>

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
};

#endif // IAPPLICATIONINTERFACE_H
