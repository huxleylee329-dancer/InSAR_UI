#ifndef IMPORTDATA_TYPES_H
#define IMPORTDATA_TYPES_H

#include <QtNodes/NodeData>
#include <QString>

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

private:
    QString _filePath;  // Path to the imported file
    QString _nodeName;  // Node name in the project tree
};

} // namespace QtNodes

#endif // IMPORTDATA_TYPES_H
