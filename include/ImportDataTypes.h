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
    explicit ImportedFileData(const QString& filePath, const QString& nodeName)
        : _filePath(filePath), _nodeName(nodeName) {}

    NodeDataType type() const override
    {
        return NodeDataType{"imported_file", "Imported File"};
    }

    QString filePath() const { return _filePath; }
    QString nodeName() const { return _nodeName; }
    void setFilePath(const QString& path) { _filePath = path; }
    void setNodeName(const QString& name) { _nodeName = name; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<ImportedFileData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_filePath.isEmpty()) return "No File";
        QFileInfo fi(_filePath);
        return fi.fileName();  // 只返回文件名
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_filePath.isEmpty()) {
            QFileInfo fi(_filePath);
            fields.append({"File", fi.fileName(), FieldEditType::None});
            fields.append({"Size", QString::number(fi.size() / 1024.0, 'f', 2) + " KB", FieldEditType::None});

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
    QString _filePath;  // Path to imported file
    QString _nodeName;  // Node name in project tree
};

} // namespace QtNodes

#endif // IMPORTDATA_TYPES_H
