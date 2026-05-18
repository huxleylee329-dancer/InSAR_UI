#ifndef NODEDATA_TYPES_H
#define NODEDATA_TYPES_H

#include <QtNodes/NodeData>
#include <opencv2/opencv.hpp>
#include <string>
#include <QFileInfo>
#include <QMap>

namespace QtNodes {

// SAR Image data type
class ImageData : public NodeData
{
public:
    ImageData() = default;
    explicit ImageData(const cv::Mat& image, const QString& filePath = "")
        : _image(image), _filePath(filePath) {}

    NodeDataType type() const override
    {
        return NodeDataType{"image", "SAR Image"};
    }

    cv::Mat image() const { return _image; }
    QString filePath() const { return _filePath; }
    void setImage(const cv::Mat& image) { _image = image; }
    void setFilePath(const QString& path) { _filePath = path; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<ImageData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_image.empty()) {
            return "Empty Image";
        }
        return QString("%1x%2").arg(_image.cols).arg(_image.rows);
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_image.empty()) {
            fields.append({"Width", QString::number(_image.cols), FieldEditType::None});
            fields.append({"Height", QString::number(_image.rows), FieldEditType::None});
            fields.append({"Channels", QString::number(_image.channels()), FieldEditType::None});
            fields.append({"Depth", QString::number(_image.depth()), FieldEditType::None});
        }
        if (!_filePath.isEmpty()) {
            QString path = _filePath;
            if (path.length() > 50) path = "..." + path.right(47);
            DataField pathField;
            pathField.key = "Path";
            pathField.value = path;
            pathField.editType = FieldEditType::Path;
            pathField.pathFilter = "Image Files (*.tif *.tiff *.png *.jpg *.jpeg);;All Files (*)";
            fields.append(pathField);
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        if (key == "Path") {
            _filePath = value;
            return true;
        }
        return false;
    }

private:
    cv::Mat _image;
    QString _filePath;
};

/**
 * @brief Universal lightweight image info data type.
 * Passes image file path and optional metadata without loading pixels.
 */
class ImageInfoData : public NodeData
{
public:
    ImageInfoData() = default;
    explicit ImageInfoData(const QString& filePath, const QMap<QString, QString>& metadata = {})
        : _filePath(filePath), _metadata(metadata) {}

    NodeDataType type() const override
    {
        return NodeDataType{"image_info", "Image Info"};
    }

    QString filePath() const { return _filePath; }
    void setFilePath(const QString& path) { _filePath = path; }

    QString getMetadata(const QString& key) const { return _metadata.value(key); }
    void setMetadata(const QString& key, const QString& value) { _metadata[key] = value; }
    QMap<QString, QString> allMetadata() const { return _metadata; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<ImageInfoData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_filePath.isEmpty()) return "No File";
        return QFileInfo(_filePath).fileName();
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_filePath.isEmpty()) {
            DataField pathField;
            pathField.key = "Path";
            pathField.value = _filePath;
            pathField.editType = FieldEditType::Path;
            pathField.pathFilter = "Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.h5);;All Files (*)";
            fields.append(pathField);
        }
        for (auto it = _metadata.begin(); it != _metadata.end(); ++it) {
            fields.append({it.key(), it.value(), FieldEditType::None});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        if (key == "Path") {
            _filePath = value;
            return true;
        }
        return false;
    }

private:
    QString _filePath;
    QMap<QString, QString> _metadata;
};

// Metadata data type (orbit information, polarization mode, etc.)
class MetadataData : public NodeData
{
public:
    MetadataData() = default;
    explicit MetadataData(const QString& metadata)
        : _metadata(metadata) {}

    NodeDataType type() const override
    {
        return NodeDataType{"metadata", "Metadata"};
    }

    QString metadata() const { return _metadata; }
    void setMetadata(const QString& metadata) { _metadata = metadata; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<MetadataData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_metadata.isEmpty()) return "Empty Metadata";
        QString summary = _metadata.left(30);
        if (_metadata.length() > 30) summary += "...";
        return summary;
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_metadata.isEmpty()) {
            QString truncated = _metadata;
            if (truncated.length() > 200) truncated = truncated.left(200) + "...";
            fields.append({"Content", truncated, FieldEditType::None});
            fields.append({"Length", QString::number(_metadata.length()) + " chars", FieldEditType::None});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        Q_UNUSED(key);
        Q_UNUSED(value);
        return false;
    }

private:
    QString _metadata;
};

// Baseline data type
class BaselineData : public NodeData
{
public:
    BaselineData() = default;
    explicit BaselineData(const QString& baselineInfo)
        : _baselineInfo(baselineInfo) {}

    NodeDataType type() const override
    {
        return NodeDataType{"baseline", "Baseline Data"};
    }

    QString baselineInfo() const { return _baselineInfo; }
    void setBaselineInfo(const QString& info) { _baselineInfo = info; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<BaselineData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_baselineInfo.isEmpty()) return "Empty Baseline";
        return _baselineInfo.left(40);
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_baselineInfo.isEmpty()) {
            fields.append({"Info", _baselineInfo, FieldEditType::Text});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        if (key == "Info") {
            _baselineInfo = value;
            return true;
        }
        return false;
    }

private:
    QString _baselineInfo;
};

// Interferometric pair list data type
class PairListData : public NodeData
{
public:
    PairListData() = default;
    explicit PairListData(const QString& pairList)
        : _pairList(pairList) {}

    NodeDataType type() const override
    {
        return NodeDataType{"pairlist", "Pair List"};
    }

    QString pairList() const { return _pairList; }
    void setPairList(const QString& list) { _pairList = list; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<PairListData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_pairList.isEmpty()) return "0 pairs";
        int pairCount = _pairList.count('\n');
        return QString("%1 pairs").arg(pairCount);
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_pairList.isEmpty()) {
            int pairCount = _pairList.count('\n');
            fields.append({"Pairs", QString::number(pairCount), FieldEditType::None});
            QString preview = _pairList.left(100);
            if (_pairList.length() > 100) preview += "...";
            fields.append({"Preview", preview, FieldEditType::None});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        Q_UNUSED(key);
        Q_UNUSED(value);
        return false;
    }

private:
    QString _pairList;
};

// Time series deformation data type
class TimeSeriesData : public NodeData
{
public:
    TimeSeriesData() = default;
    explicit TimeSeriesData(const QString& timeSeries)
        : _timeSeries(timeSeries) {}

    NodeDataType type() const override
    {
        return NodeDataType{"timeseries", "Time Series Data"};
    }

    QString timeSeries() const { return _timeSeries; }
    void setTimeSeries(const QString& series) { _timeSeries = series; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<TimeSeriesData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_timeSeries.isEmpty()) return "0 time points";
        int pointCount = _timeSeries.count('\n');
        return QString("%1 time points").arg(pointCount);
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_timeSeries.isEmpty()) {
            int pointCount = _timeSeries.count('\n');
            fields.append({"Time Points", QString::number(pointCount), FieldEditType::None});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        Q_UNUSED(key);
        Q_UNUSED(value);
        return false;
    }

private:
    QString _timeSeries;
};

// Coordinate transformation matrix data type
class CoordinateMatrixData : public NodeData
{
public:
    CoordinateMatrixData() = default;
    explicit CoordinateMatrixData(const QString& matrixInfo)
        : _matrixInfo(matrixInfo) {}

    NodeDataType type() const override
    {
        return NodeDataType{"coordinate", "Coordinate Matrix"};
    }

    QString matrixInfo() const { return _matrixInfo; }
    void setMatrixInfo(const QString& info) { _matrixInfo = info; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<CoordinateMatrixData const *>(&nodeData);
        return d != nullptr;
    }

    QString getSummary() const override
    {
        if (_matrixInfo.isEmpty()) return "Empty Matrix";
        return _matrixInfo.left(40);
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_matrixInfo.isEmpty()) {
            fields.append({"Info", _matrixInfo, FieldEditType::Text});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        if (key == "Info") {
            _matrixInfo = value;
            return true;
        }
        return false;
    }

private:
    QString _matrixInfo;
};

} // namespace QtNodes

#endif // NODEDATA_TYPES_H
