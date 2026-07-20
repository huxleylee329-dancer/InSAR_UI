#include "QtNodes/internal/NodeDataSnapshot.hpp"
#include "QtNodes/BasicGraphicsScene"
#include "QtNodes/DataFlowGraphModel"
#include "QtNodes/NodeDelegateModel"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QtWidgets/QApplication>
#include <QDebug>

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
        info.portType = PortType::In;
        info.showIndex = (inPortCount > 1);
        info.name = getPortName(graphModel, nodeId, PortType::In, i);
        info.dataType = graphModel.portData(nodeId, PortType::In, i, PortRole::DataType)
                                        .value<NodeDataType>().name;
        info.isConnected = !graphModel.connections(nodeId, PortType::In, i).empty();

        // Capture summary and fields if connected
        if (info.isConnected) {
            auto data = model->getInputData(i);
            if (data) {
                info.summary = data->getSummary();
                info.fields = data->getFields();
            }
        }
        snapshot.inputPorts.push_back(info);
    }

    // Capture output ports data
    unsigned int outPortCount = graphModel.nodeData<unsigned int>(
        nodeId, NodeRole::OutPortCount);
    for (PortIndex i = 0; i < outPortCount; ++i) {
        PortDataInfo info;
        info.index = i;
        info.portType = PortType::Out;
        info.showIndex = (outPortCount > 1);
        info.name = getPortName(graphModel, nodeId, PortType::Out, i);
        info.dataType = graphModel.portData(nodeId, PortType::Out, i, PortRole::DataType)
                                        .value<NodeDataType>().name;
        info.isConnected = !graphModel.connections(nodeId, PortType::Out, i).empty();

        // Capture summary and fields
        auto data = model->getOutputData(i);
        if (data) {
            info.summary = data->getSummary();
            info.fields = data->getFields();
        }
        snapshot.outputPorts.push_back(info);
    }

    // Capture node parameters (same as PropertyEditor)
    snapshot.parameters = model->getParameters();

    // Processing info for detail view middle column
    snapshot.processingInfo = model->processingInfo();

    // Capture preview image path and detection results if completed or if it supports ROI selection/Two ROIs
    if (model->executionState() == ExecutionState::Completed ||
        model->executionState() == ExecutionState::Warning ||
        model->supportsRoiSelection() || model->supportsTwoRois()) {
        snapshot.previewImagePaths = model->previewImagePaths();
        snapshot.detectionResults = model->detectionResults();
    } else {
        snapshot.previewImagePaths.clear();
        snapshot.detectionResults.clear();
    }
    snapshot.supportsRoiSelection = model->supportsRoiSelection();
    snapshot.detailTableHeaders = model->detailTableHeaders();
    snapshot.hasCustomRoi = model->hasCustomRoi();
    snapshot.customRoi = model->customRoi();
    
    snapshot.supportsTwoRois = model->supportsTwoRois();
    snapshot.hasTargetRoi = model->hasTargetRoi();
    snapshot.targetRoi = model->targetRoi();
    snapshot.hasClutterRoi = model->hasClutterRoi();
    snapshot.clutterRoi = model->clutterRoi();

    return snapshot;
}

} // namespace QtNodes
