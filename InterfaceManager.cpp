#include "InterfaceManager.h"
#include "IApplicationInterface.h"
#include <FormatConversion.h>
#include "tinyxml.h"
#include <QSettings>
#include <QToolBar>
#include <QMainWindow>
#include <QAction>

InterfaceManager::InterfaceManager(QMainWindow *mainWindow)
    : m_mainWindow(mainWindow)
{
}

InterfaceManager::~InterfaceManager()
{
    // Delete all interface instances we own
    qDeleteAll(m_interfaces);
    m_interfaces.clear();
}

void InterfaceManager::registerInterface(IApplicationInterface *appInterface)
{
    if (!m_interfaces.contains(appInterface)) {
        m_interfaces.append(appInterface);
    }
}

bool InterfaceManager::switchToInterface(const QString &interfaceId)
{
    // Find the interface
    IApplicationInterface *newInterface = nullptr;
    for (IApplicationInterface *iface : m_interfaces) {
        if (iface->id() == interfaceId) {
            newInterface = iface;
            break;
        }
    }

    if (!newInterface || newInterface == m_currentInterface) {
        return false;
    }

    // Deactivate current interface
    if (m_currentInterface) {
        m_currentInterface->deactivate();
        removeCurrentToolBars();
    }

    // Remove current central widget from MainWindow's ownership to prevent deletion
    // setCentralWidget() deletes the old widget if it has no other parent
    QWidget* oldCentralWidget = m_mainWindow->centralWidget();
    if (oldCentralWidget) {
        oldCentralWidget->setParent(nullptr);
    }

    // Set new central widget
    m_mainWindow->setCentralWidget(newInterface->centralWidget());

    // Add toolbars
    addInterfaceToolBars(newInterface);

    // 同步项目上下文到新界面
    if (m_projectModel) {
        newInterface->setProjectContext(m_projectModel, m_projectPath, m_projectName, m_projectXml);
    }

    // Activate new interface
    newInterface->activate();

    m_currentInterface = newInterface;

    // Save as global default
    saveDefaultInterface(interfaceId);

    return true;
}

IApplicationInterface* InterfaceManager::currentInterface() const
{
    return m_currentInterface;
}

QString InterfaceManager::currentInterfaceId() const
{
    return m_currentInterface ? m_currentInterface->id() : QString();
}

QString InterfaceManager::loadDefaultInterface() const
{
    QSettings settings("Config.ini", QSettings::IniFormat);
    return settings.value("Interface/Default", "").toString();
}

void InterfaceManager::saveDefaultInterface(const QString &interfaceId) const
{
    // Do not save "welcome" as default interface
    if (interfaceId == "welcome") return;

    QSettings settings("Config.ini", QSettings::IniFormat);
    settings.setValue("Interface/Default", interfaceId);
}

QString InterfaceManager::loadLastInterfaceFromProject(XMLFile *projectXml) const
{
    if (!projectXml) {
        return QString();
    }

    // Read from XML - implementation depends on XMLFile structure
    // We'll look for /project/lastInterface
    TiXmlElement* root = nullptr;
    projectXml->get_root(root);
    if (!root) {
        return QString();
    }

    TiXmlElement* pnode = nullptr;
    projectXml->_find_node(root, "lastInterface", pnode);
    if (pnode && pnode->GetText()) {
        return QString(pnode->GetText());
    }

    return QString();
}

void InterfaceManager::saveLastInterfaceToProject(XMLFile *projectXml) const
{
    if (!projectXml || !m_currentInterface) {
        return;
    }

    // Get root node
    TiXmlElement* root = nullptr;
    projectXml->get_root(root);
    if (!root) {
        return;
    }

    // Find or create lastInterface node
    TiXmlElement* pnode = nullptr;
    projectXml->_find_node(root, "lastInterface", pnode);
    if (!pnode) {
        pnode = new TiXmlElement("lastInterface");
        root->LinkEndChild(pnode);
    }

    // Set the text (must Clear first, otherwise TiXmlText appends and causes concatenation)
    pnode->Clear();
    std::string interfaceId = m_currentInterface->id().toStdString();
    TiXmlText* textNode = new TiXmlText(interfaceId.c_str());
    pnode->LinkEndChild(textNode);
}

void InterfaceManager::removeCurrentToolBars()
{
    if (!m_currentInterface) {
        return;
    }

    for (QToolBar *toolbar : m_currentInterface->toolBars()) {
        m_mainWindow->removeToolBar(toolbar);
    }
}

void InterfaceManager::addInterfaceToolBars(IApplicationInterface *appInterface)
{
    bool showToolBar = true;
    QAction* showToolBarAction = m_mainWindow->findChild<QAction*>("actionShowToolBar");
    if (showToolBarAction) {
        showToolBar = showToolBarAction->isChecked();
    }

    for (QToolBar *toolbar : appInterface->toolBars()) {
        m_mainWindow->addToolBar(toolbar);
        toolbar->setVisible(showToolBar);
    }
}

void InterfaceManager::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml)
{
    m_projectModel = model;
    m_projectPath = path;
    m_projectName = name;
    m_projectXml = projectXml;

    // 同步到当前界面
    if (m_currentInterface) {
        m_currentInterface->setProjectContext(model, path, name, projectXml);
    }
}
