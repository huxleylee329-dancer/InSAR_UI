#ifndef IMPORTDATA_TYPES_H
#define IMPORTDATA_TYPES_H

#include <QtNodes/NodeData>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QJsonObject>

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
                fields.append({"Paths", _filePaths.join("\n"), FieldEditType::None});
            } else {
                QFileInfo fi(_filePaths.first());
                if (fi.isDir()) {
                    QDir dir(fi.absoluteFilePath());
                    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
                    int h5Count = 0;
                    int previewCount = 0;
                    qint64 totalSize = 0;
                    for (const QFileInfo& fileInfo : files) {
                        QString suffix = fileInfo.suffix().toLower();
                        if (suffix == "h5") {
                            ++h5Count;
                            totalSize += fileInfo.size();
                        } else if (suffix == "jpg" || suffix == "jpeg" || suffix == "png" || suffix == "bmp") {
                            ++previewCount;
                        }
                    }

                    QString sizeStr;
                    double sizeMB = totalSize / (1024.0 * 1024.0);
                    if (sizeMB > 1024.0) {
                        sizeStr = QString::number(sizeMB / 1024.0, 'f', 2) + " GB";
                    } else {
                        sizeStr = QString::number(sizeMB, 'f', 2) + " MB";
                    }

                    fields.append({"Folder", fi.fileName(), FieldEditType::None});
                    fields.append({"Path", fi.absoluteFilePath(), FieldEditType::None});
                    fields.append({"H5 Files", QString::number(h5Count), FieldEditType::None});
                    fields.append({"Previews", QString::number(previewCount), FieldEditType::None});
                    fields.append({"Total Size", sizeStr, FieldEditType::None});
                } else {
                    fields.append({"File", fi.fileName(), FieldEditType::None});
                    fields.append({"Path", fi.absoluteFilePath(), FieldEditType::None});
                    fields.append({"Size", QString::number(fi.size() / 1024.0, 'f', 2) + " KB", FieldEditType::None});
                }
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

// ============================================================================
// DEMFileData - Data type for DEM files
// ============================================================================
class DEMFileData : public ImportedFileData
{
public:
    using ImportedFileData::ImportedFileData;

    DEMFileData(const QString& rasterPath, const QString& nodeName, const QString& identityH5Path)
        : ImportedFileData(rasterPath, nodeName)
        , _identityH5Path(identityH5Path)
    {
    }

    QString identityH5Path() const { return _identityH5Path; }

    NodeDataType type() const override
    {
        return NodeDataType{"dem_file", "DEM File"};
    }

    bool sameType(NodeData const &nodeData) const override
    {
        return dynamic_cast<DEMFileData const *>(&nodeData) != nullptr;
    }

private:
    QString _identityH5Path;
};

// Committed auxiliary terrain DEM entity.  Paths are only exposed to local
// consumers after the resource registry has resolved the binding.
class AuxiliaryDemData : public NodeData
{
public:
    AuxiliaryDemData() = default;
    AuxiliaryDemData(const QString& rasterPath,
                     const QString& identityH5Path,
                     const QString& resourceId,
                     const QString& pinnedProvenanceId,
                     const QString& nodeName = QString(),
                     const QString& validMaskPath = QString())
        : _rasterPath(rasterPath), _identityH5Path(identityH5Path),
          _resourceId(resourceId), _pinnedProvenanceId(pinnedProvenanceId),
          _nodeName(nodeName), _validMaskPath(validMaskPath) {}

    NodeDataType type() const override
    {
        return NodeDataType{"auxiliary_dem", "Auxiliary DEM"};
    }
    QString rasterPath() const { return _rasterPath; }
    QString identityH5Path() const { return _identityH5Path; }
    QString validMaskPath() const { return _validMaskPath; }
    QString resourceId() const { return _resourceId; }
    QString pinnedProvenanceId() const { return _pinnedProvenanceId; }
    QString nodeName() const { return _nodeName; }
    bool isValid() const
    {
        return !_rasterPath.isEmpty() && !_identityH5Path.isEmpty() &&
               !_resourceId.isEmpty() && !_pinnedProvenanceId.isEmpty();
    }
    bool sameType(NodeData const &nodeData) const override
    {
        return dynamic_cast<AuxiliaryDemData const *>(&nodeData) != nullptr;
    }
    QString getSummary() const override
    {
        return _resourceId.isEmpty() ? QStringLiteral("Unresolved Auxiliary DEM") : _resourceId;
    }
    QVector<DataField> getFields() const override
    {
        return QVector<DataField>{
            {QStringLiteral("Resource"), _resourceId, FieldEditType::None},
            {QStringLiteral("Provenance"), _pinnedProvenanceId, FieldEditType::None},
            {QStringLiteral("Raster"), _rasterPath, FieldEditType::None},
            {QStringLiteral("Identity H5"), _identityH5Path, FieldEditType::None},
            {QStringLiteral("Validity mask"), _validMaskPath, FieldEditType::None}};
    }

private:
    QString _rasterPath;
    QString _identityH5Path;
    QString _validMaskPath;
    QString _resourceId;
    QString _pinnedProvenanceId;
    QString _nodeName;
};

// Remote/resource-reference DEM data.  This type intentionally carries no
// physical path; consumers must resolve it through the project registry.
class AuxiliaryDemReferenceData : public NodeData
{
public:
    AuxiliaryDemReferenceData() = default;
    AuxiliaryDemReferenceData(const QString& resourceId,
                              const QString& pinnedProvenanceId,
                              int schemaVersion = 1)
        : _resourceId(resourceId), _pinnedProvenanceId(pinnedProvenanceId),
          _schemaVersion(schemaVersion) {}

    NodeDataType type() const override
    {
        return NodeDataType{"auxiliary_dem_reference", "Auxiliary DEM Reference"};
    }
    QString role() const { return QStringLiteral("auxiliary_terrain_dem"); }
    QString resourceId() const { return _resourceId; }
    QString pinnedProvenanceId() const { return _pinnedProvenanceId; }
    int schemaVersion() const { return _schemaVersion; }
    bool isValid() const
    {
        return !_resourceId.isEmpty() && !_pinnedProvenanceId.isEmpty() && _schemaVersion > 0;
    }
    bool sameType(NodeData const &nodeData) const override
    {
        return dynamic_cast<AuxiliaryDemReferenceData const *>(&nodeData) != nullptr;
    }
    QString getSummary() const override
    {
        return _resourceId.isEmpty() ? QStringLiteral("Unresolved DEM Reference") : _resourceId;
    }
    QVector<DataField> getFields() const override
    {
        return QVector<DataField>{
            {QStringLiteral("Role"), role(), FieldEditType::None},
            {QStringLiteral("Resource"), _resourceId, FieldEditType::None},
            {QStringLiteral("Provenance"), _pinnedProvenanceId, FieldEditType::None},
            {QStringLiteral("Schema"), QString::number(_schemaVersion), FieldEditType::None}};
    }

private:
    QString _resourceId;
    QString _pinnedProvenanceId;
    int _schemaVersion = 1;
};

class InsarDemData : public NodeData
{
public:
    enum class Availability { Executable, HistoricalReadOnly };
    InsarDemData() = default;
    InsarDemData(const QString& h5Path,
                 const QString& runId,
                 Availability availability = Availability::Executable)
        : _h5Paths(QStringList() << h5Path), _runId(runId), _availability(availability) {}
    InsarDemData(const QStringList& h5Paths,
                 const QString& runId,
                 Availability availability = Availability::Executable)
        : _h5Paths(h5Paths), _runId(runId), _availability(availability) {}

    NodeDataType type() const override
    {
        return NodeDataType{"insar_dem", "InSAR DEM"};
    }
    QString h5Path() const { return _h5Paths.isEmpty() ? QString() : _h5Paths.first(); }
    QStringList h5Paths() const { return _h5Paths; }
    QString runId() const { return _runId; }
    Availability availability() const { return _availability; }
    bool isExecutable() const
    {
        return _availability == Availability::Executable && !_h5Paths.isEmpty() && !_runId.isEmpty();
    }
    bool sameType(NodeData const &nodeData) const override
    {
        return dynamic_cast<InsarDemData const *>(&nodeData) != nullptr;
    }
    QString getSummary() const override
    {
        return _h5Paths.isEmpty() ? QStringLiteral("Unresolved InSAR DEM") : QFileInfo(h5Path()).fileName();
    }
    QVector<DataField> getFields() const override
    {
        return QVector<DataField>{
            {QStringLiteral("H5"), _h5Paths.join(QStringLiteral("\n")), FieldEditType::None},
            {QStringLiteral("Run"), _runId, FieldEditType::None},
            {QStringLiteral("Availability"),
             _availability == Availability::Executable ? QStringLiteral("executable") : QStringLiteral("historical_read_only"),
             FieldEditType::None}};
    }

private:
    QStringList _h5Paths;
    QString _runId;
    Availability _availability = Availability::Executable;
};

} // namespace QtNodes

#endif // IMPORTDATA_TYPES_H
