#pragma once

#include <FormatConversion.h>
#include <QFileInfo>
#include <QStringList>
#include <QStandardItemModel>
#include "NodeUtils.h"
#include "icon_source.h"

namespace ImportOutputPersistence {

inline bool persist(QStandardItemModel* model,
                    const QString& projectName,
                    const QString& savePath,
                    const QString& dstNode,
                    const QStringList& outputNames,
                    const QStringList& outputPaths,
                    const QString& dataType,
                    const QString& satelliteFormat,
                    XMLFile* projectXml = nullptr)
{
    if (!model || projectName.isEmpty() || savePath.isEmpty() || dstNode.isEmpty() ||
        outputNames.isEmpty() || outputNames.size() != outputPaths.size()) {
        return false;
    }

    const QList<QStandardItem*> projects = model->findItems(projectName);
    if (projects.isEmpty()) {
        return false;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), dstNode, "complex-0.0", FOLDER_ICON);
    if (!outputNode) {
        return false;
    }

    XMLFile localXml;
    XMLFile* xml = projectXml ? projectXml : &localXml;
    const QString xmlPath = savePath + "/" + projectName;
    if (!projectXml && xml->XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
        return false;
    }

    for (int i = 0; i < outputPaths.size(); ++i) {
        bool created = false;
        QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
            outputNode, outputNames[i], dataType, outputPaths[i], IMAGEDATA_ICON, &created);
        if (!imageItem) {
            return false;
        }
        if (!created) {
            outputNode->setChild(imageItem->row(), 1, new QStandardItem(outputPaths[i]));
            continue;
        }

        const QString relativePath = QString("/%1/%2").arg(dstNode, QFileInfo(outputPaths[i]).fileName());
        xml->XMLFile_add_origin(dstNode.toStdString().c_str(), outputNames[i].toStdString().c_str(),
            relativePath.toStdString().c_str(), satelliteFormat.toStdString().c_str());
    }

    xml->XMLFile_save(xmlPath.toStdString().c_str());
    return true;
}

} // namespace ImportOutputPersistence
