#pragma once

#include <QWidget>
#include <QApplication>
#include <QFileInfo>
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "FormatConversion.h"

namespace NodeUtils {

/**
 * @brief Traverses widgets to find the application context (WorkspaceUI/MainWindow)
 * @param widget The node widget or any reference widget
 * @return Pointer to IApplicationInterface or nullptr
 */
inline IApplicationInterface* getProjectContext(QWidget* widget)
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

/**
 * @brief Remove a DataNode entry from the project tree model and XML file.
 *
 * Call this when a node's output name changes so the old entry doesn't
 * remain as a stale reference (which would prevent orphan detection).
 *
 * @param iface  Project context (IApplicationInterface*)
 * @param oldNodeName  The old DataNode name to remove
 */
inline void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName)
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
                break;
            }
        }
    }

    // Also remove from XML file
    // Determine the XML file path. projectPath() from IApplicationInterface
    // returns the full path to the .Insar file (e.g. "D:/projects/test.Insar")
    QString xmlPath = projPath;
    // If projPath doesn't end with projName, construct it
    if (!xmlPath.endsWith(projName)) {
        // projPath is the directory, projName is the filename
        xmlPath = projPath + "/" + projName;
    }

    XMLFile xml;
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) >= 0) {
        // Find and remove the DataNode element with the old name
        TiXmlElement* root = nullptr;
        xml.get_root(root);
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
        xml.XMLFile_save(xmlPath.toStdString().c_str());
    }
}

} // namespace NodeUtils

