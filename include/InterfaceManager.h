#ifndef INTERFACEMANAGER_H
#define INTERFACEMANAGER_H

#include "IApplicationInterface.h"
#include <QMainWindow>
#include <QList>
#include <QSettings>
#include <QString>

/**
 * @brief 界面管理器
 *
 * 管理所有可切换的应用界面，处理切换逻辑和持久化
 */
class InterfaceManager
{
public:
    explicit InterfaceManager(QMainWindow *mainWindow);
    ~InterfaceManager();

    /**
     * @brief 注册界面
     * @param appInterface 界面实例指针
     */
    void registerInterface(IApplicationInterface *appInterface);

    /**
     * @brief 切换到指定界面
     * @param interfaceId 界面ID
     * @return 是否成功切换
     */
    bool switchToInterface(const QString &interfaceId);

    /**
     * @brief 获取当前界面
     * @return 当前界面指针
     */
    IApplicationInterface* currentInterface() const;

    /**
     * @brief 获取当前界面ID
     * @return 当前界面ID
     */
    QString currentInterfaceId() const;

    /**
     * @brief 获取所有已注册的界面列表
     * @return 界面列表
     */
    QList<IApplicationInterface*> interfaces() const;

    /**
     * @brief 读取全局默认界面
     * @return 默认界面ID，如果没有设置返回空
     */
    QString loadDefaultInterface() const;

    /**
     * @brief 保存全局默认界面
     * @param interfaceId 界面ID
     */
    void saveDefaultInterface(const QString &interfaceId) const;

    /**
     * @brief 获取项目文件中存储的上次界面
     * @param projectXml 项目XML对象
     * @return 上次界面ID，如果没有存储返回空
     */
    QString loadLastInterfaceFromProject(class XMLFile *projectXml) const;

    /**
     * @brief 保存当前界面到项目XML
     * @param projectXml 项目XML对象
     */
    void saveLastInterfaceToProject(class XMLFile *projectXml) const;

private:
    void removeCurrentToolBars();
    void addInterfaceToolBars(IApplicationInterface *appInterface);

    QMainWindow *m_mainWindow;
    QList<IApplicationInterface*> m_interfaces;
    IApplicationInterface *m_currentInterface = nullptr;
};

#endif // INTERFACEMANAGER_H
