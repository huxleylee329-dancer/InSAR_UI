#include "include/NodeUtils.h"
#include <QWidget>
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QDir>
#include "include/IApplicationInterface.h"
#include "include/MainWindow.h"
#include "include/WorkspaceUI.h"
#include "include/InterfaceManager.h"
#include "include/FormatConversion.h"
#include <Utils.h>
#include <cmath>

namespace NodeUtils {

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

bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type)
{
    if (h5Path.isEmpty() || jpgPath.isEmpty())
        return false;

    if (type == "complex")
    {
        Utils util;
        ComplexMat SLC64;
        FormatConversion FC;
        
        if (FC.read_slc_from_h5(h5Path.toLocal8Bit().constData(), SLC64) != 0)
            return false;
            
        util.saveSLC(jpgPath.toLocal8Bit().constData(), 65, SLC64);
        
        if (SLC64.GetCols() * SLC64.GetRows() > 25e6)
        {
            int down_sample_times = (int)std::sqrt(std::floor(double(SLC64.GetCols() * SLC64.GetRows()) / 25e6));
            if (down_sample_times > 1) {
                util.resampling(jpgPath.toLocal8Bit().constData(), jpgPath.toLocal8Bit().constData(),
                    (int)(SLC64.GetRows() / down_sample_times),
                    (int)(SLC64.GetCols() / down_sample_times));
            }
        }
        return true;
    }
    else if (type == "phase")
    {
        FormatConversion FC;
        Utils util;
        cv::Mat phase;
        
        if (FC.read_array_from_h5(h5Path.toLocal8Bit().constData(), "phase", phase) != 0)
            return false;
            
        int ret = util.savephase(jpgPath.toLocal8Bit().constData(), "jet", phase);
        if (ret && phase.rows * phase.cols > 25e6)
        {
            int down_sample_times = (int)std::sqrt(std::floor(double(phase.rows * phase.cols) / 25e6));
            if (down_sample_times > 1) {
                util.resampling(jpgPath.toLocal8Bit().constData(), jpgPath.toLocal8Bit().constData(),
                    (int)(phase.rows / down_sample_times),
                    (int)(phase.cols / down_sample_times));
            }
        }
        return ret != 0;
    }
    return false;
}

} // namespace NodeUtils
