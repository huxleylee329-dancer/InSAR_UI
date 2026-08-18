#pragma once

#include "Export.hpp"
#include "Definitions.hpp"
#include "NodeData.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include <vector>
#include <string>
#include <QString>
#include <QObject>
#include <QVector>

namespace QtNodes {

// Forward declarations
class ExecutableNodeDelegateModel;
class BasicGraphicsScene;
class AbstractGraphModel;
class NodeData;

/// Structure representing a port's data for display in detail view
struct NODE_EDITOR_PUBLIC PortDataInfo
{
    PortIndex index;
    PortType portType;
    QString name;
    QString dataType;
    QString summary;
    QVector<DataField> fields;
    bool isConnected;
    bool showIndex;
    bool isBound = false;
    QString bindingSummary;
};

/// Snapshot of node state and data for display in detail view
struct NODE_EDITOR_PUBLIC NodeDataSnapshot
{
    QString nodeName;
    int mode;
    int state;
    int progress;
    std::vector<PortDataInfo> inputPorts;
    std::vector<PortDataInfo> outputPorts;
    QVector<ParameterInfo> parameters;

    /// Processing/intermediate information
    std::vector<QString> processingInfo;

    /// Path to a preview image (e.g. from Generic SAR Import)
    QStringList previewImagePaths;
    QList<QStringList> detectionResults;
    bool supportsRoiSelection;
    QStringList detailTableHeaders;
    bool hasCustomRoi;
    QRectF customRoi;
    
    // Dual ROI support (e.g. for Target/Clutter)
    bool supportsTwoRois;
    bool hasTargetRoi;
    QRectF targetRoi;
    bool hasClutterRoi;
    QRectF clutterRoi;

    /// Helper to convert state to string
    static QString stateToString(int state)
    {
        switch (state) {
        case 0: return QObject::tr("Idle");
        case 1: return QObject::tr("Pending");
        case 2: return QObject::tr("Running");
        case 3: return QObject::tr("Completed");
        case 4: return QObject::tr("Stopped");
        case 5: return QObject::tr("Warning");
        case 6: return QObject::tr("Error");
        case 7: return QObject::tr("Disabled");
        }
        return QObject::tr("Unknown");
    }
};

/// Capture a snapshot of node data for display in detail view
NODE_EDITOR_PUBLIC NodeDataSnapshot captureNodeData(ExecutableNodeDelegateModel* model,
                                                     BasicGraphicsScene* scene,
                                                     NodeId nodeId);

} // namespace QtNodes
