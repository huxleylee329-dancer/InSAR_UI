#ifndef NODEDATA_TYPES_H
#define NODEDATA_TYPES_H

#include <QtNodes/NodeData>
#include <opencv2/opencv.hpp>
#include <string>

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

private:
    cv::Mat _image;
    QString _filePath;
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

private:
    QString _matrixInfo;
};

} // namespace QtNodes

#endif // NODEDATA_TYPES_H
