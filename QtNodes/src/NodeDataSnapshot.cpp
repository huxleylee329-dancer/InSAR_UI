#include "QtNodes/internal/NodeDataSnapshot.hpp"
#include "QtNodes/BasicGraphicsScene"
#include "QtNodes/DataFlowGraphModel"
#include "QtNodes/NodeDelegateModel"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QtWidgets/QApplication>

namespace QtNodes {

// Helper functions in anonymous namespace
namespace {
    QString nodeDataToString(std::shared_ptr<NodeData> data)
    {
        if (!data) {
            return QObject::tr("<null>");
        }
        return QString("(%1)").arg(data->type().id);
    }

    QString getPortName(AbstractGraphModel& model, NodeId nodeId,
                                   PortType portType, PortIndex portIndex)
    {
        if (model.portData<bool>(nodeId, portType, portIndex, PortRole::CaptionVisible)) {
            return model.portData<QString>(nodeId, portType, portIndex, PortRole::Caption);
        } else {
            auto portData = model.portData(nodeId, portType, portIndex, PortRole::DataType);
            return portData.value<NodeDataType>().name;
        }
    }
}

NODE_EDITOR_PUBLIC NodeDataSnapshot captureNodeData(ExecutableNodeDelegateModel* model,
                                  BasicGraphicsScene* scene,
                                  NodeId nodeId)
{
    NodeDataSnapshot snapshot;

    if (!model || !scene) {
        return snapshot;
    }

    AbstractGraphModel& graphModel = scene->graphModel();

    // Capture basic node information
    auto nodeNameData = graphModel.nodeData(nodeId, NodeRole::Caption);
    snapshot.nodeName = nodeNameData.toString();
    snapshot.mode = static_cast<int>(model->executionMode());
    snapshot.state = static_cast<int>(model->executionState());
    snapshot.progress = model->progress();

    // Capture input ports data
    unsigned int inPortCount = graphModel.nodeData<unsigned int>(
        nodeId, NodeRole::InPortCount);
    for (PortIndex i = 0; i < inPortCount; ++i) {
        PortDataInfo info;
        info.index = i;
        info.name = getPortName(graphModel, nodeId, PortType::In, i);
        info.dataType = graphModel.portData(nodeId, PortType::In, i, PortRole::DataType)
                                        .value<NodeDataType>().name;
        info.value = nodeDataToString(model->getInputData(i));
        info.isConnected = !graphModel.connections(nodeId, PortType::In, i).empty();
        snapshot.inputPorts.push_back(info);
    }

    // Capture output ports data
    unsigned int outPortCount = graphModel.nodeData<unsigned int>(
        nodeId, NodeRole::OutPortCount);
    for (PortIndex i = 0; i < outPortCount; ++i) {
        PortDataInfo info;
        info.index = i;
        info.name = getPortName(graphModel, nodeId, PortType::Out, i);
        info.dataType = graphModel.portData(nodeId, PortType::Out, i, PortRole::DataType)
                                        .value<NodeDataType>().name;
        info.value = nodeDataToString(model->getOutputData(i));
        info.isConnected = !graphModel.connections(nodeId, PortType::Out, i).empty();
        snapshot.outputPorts.push_back(info);
    }

    // Processing info placeholder
    if (snapshot.state == static_cast<int>(ExecutionState::Running) ||
        snapshot.state == static_cast<int>(ExecutionState::Completed)) {
        snapshot.processingInfo.push_back(QObject::tr("Node is processing..."));
    } else if (snapshot.state == static_cast<int>(ExecutionState::Error)) {
        snapshot.processingInfo.push_back(QObject::tr("Execution failed"));
    }

    return snapshot;
}

} // namespace QtNodes
