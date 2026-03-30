#pragma once

#include "Export.hpp"
#include "Definitions.hpp"
#include <vector>
#include <string>
#include <QString>
#include <QObject>

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
    QString name;
    QString dataType;
    QString value;
    bool isConnected;
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

    /// Processing/intermediate information
    std::vector<QString> processingInfo;

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
