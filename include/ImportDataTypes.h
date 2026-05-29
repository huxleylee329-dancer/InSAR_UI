#ifndef IMPORTDATA_TYPES_H
#define IMPORTDATA_TYPES_H

#include <QtNodes/NodeData>
#include <QString>
#include <QFileInfo>

namespace QtNodes {

// ============================================================================
// ImportedFileData - Data type for imported files
// ============================================================================
class ImportedFileData : public NodeData
{
public:
    ImportedFileData() = default;
    
    // 兼容老版本的单文件构造函数
    explicit ImportedFileData(const QString& filePath, const QString& nodeName)
        : _nodeName(nodeName) 
    {
        if (!filePath.isEmpty()) {
            _filePaths.append(filePath);
        }
    }
    
    // 新增的多文件构造函数
    explicit ImportedFileData(const QStringList& filePaths, const QString& nodeName)
        : _filePaths(filePaths), _nodeName(nodeName) {}

    NodeDataType type() const override
    {
        return NodeDataType{"imported_file", "Imported File"};
    }

    // 向下兼容：如果下游只接受单文件，给它第一个
    QString filePath() const { return _filePaths.isEmpty() ? QString() : _filePaths.first(); }
    
    // 获取全部文件列表
    QStringList filePaths() const { return _filePaths; }
    
    QString nodeName() const { return _nodeName; }
    
    void setFilePath(const QString& path) { 
        _filePaths.clear(); 
        if (!path.isEmpty()) _filePaths.append(path); 
    }
    void setFilePaths(const QStringList& paths) { _filePaths = paths; }
    void setNodeName(const QString& name) { _nodeName = name; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<ImportedFileData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_filePaths.isEmpty()) return "No File";
        if (_filePaths.size() > 1) {
            return QString("Batch: %1 Files").arg(_filePaths.size());
        }
        QFileInfo fi(_filePaths.first());
        return fi.fileName();
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_filePaths.isEmpty()) {
            if (_filePaths.size() > 1) {
                // 批量文件显示
                fields.append({"Items", QString::number(_filePaths.size()) + " Files", FieldEditType::None});
                
                // 计算总体积
                qint64 totalSize = 0;
                for (const QString& path : _filePaths) {
                    QFileInfo fi(path);
                    totalSize += fi.size();
                }
                
                QString sizeStr;
                double sizeMB = totalSize / (1024.0 * 1024.0);
                if (sizeMB > 1024.0) {
                    sizeStr = QString::number(sizeMB / 1024.0, 'f', 2) + " GB";
                } else {
                    sizeStr = QString::number(sizeMB, 'f', 2) + " MB";
                }
                fields.append({"Total Size", sizeStr, FieldEditType::None});
            } else {
                // 单文件显示
                QFileInfo fi(_filePaths.first());
                fields.append({"File", fi.fileName(), FieldEditType::None});
                fields.append({"Size", QString::number(fi.size() / 1024.0, 'f', 2) + " KB", FieldEditType::None});
            }

            // 可编辑：节点名称
            DataField nameField;
            nameField.key = "Node";
            nameField.value = _nodeName;
            nameField.editType = FieldEditType::Text;
            fields.append(nameField);
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        if (key == "Node") {
            _nodeName = value;
            return true;
        }
        return false;
    }

private:
    QStringList _filePaths;  // Paths to imported files
    QString _nodeName;       // Node name in project tree
};

} // namespace QtNodes

#endif // IMPORTDATA_TYPES_H
