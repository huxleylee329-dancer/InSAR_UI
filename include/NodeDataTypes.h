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
        : _filePaths(QStringList() << filePath), _metadata(metadata) {}
    explicit ImageInfoData(const QStringList& filePaths, const QMap<QString, QString>& metadata = {})
        : _filePaths(filePaths), _metadata(metadata) {}

    NodeDataType type() const override
    {
        return NodeDataType{"image_info", "Image Info"};
    }

    QString filePath() const { return _filePaths.isEmpty() ? QString() : _filePaths.first(); }
    void setFilePath(const QString& path) { _filePaths = QStringList() << path; }
    
    QStringList filePaths() const { return _filePaths; }
    void setFilePaths(const QStringList& paths) { _filePaths = paths; }

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
        if (_filePaths.isEmpty()) return "No File";
        if (_filePaths.size() > 1) return QString("%1 Images").arg(_filePaths.size());
        return QFileInfo(_filePaths.first()).fileName();
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        if (!_filePaths.isEmpty()) {
            DataField pathField;
            pathField.key = "Path";
            if (_filePaths.size() == 1) {
                pathField.value = _filePaths.first();
            } else {
                if (_filePaths.size() <= 4) {
                    pathField.value = _filePaths.join("\n");
                } else {
                    QStringList preview;
                    preview << _filePaths[0];
                    preview << _filePaths[1];
                    preview << QString("... (共 %1 个文件) ...").arg(_filePaths.size());
                    preview << _filePaths[_filePaths.size() - 2];
                    preview << _filePaths.last();
                    pathField.value = preview.join("\n");
                }
            }
            pathField.editType = _filePaths.size() == 1 ? FieldEditType::Path : FieldEditType::None;
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
            _filePaths = QStringList() << value;
            return true;
        }
        return false;
    }

private:
    QStringList _filePaths;
    QMap<QString, QString> _metadata;
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

} // namespace QtNodes

#endif // NODEDATA_TYPES_H
