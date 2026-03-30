#pragma once

#include "DefaultHorizontalNodeGeometry.hpp"

namespace QtNodes {

class AbstractGraphModel;
class ExecutableNodeDelegateModel;

class NODE_EDITOR_PUBLIC ExecutableNodeGeometry : public DefaultHorizontalNodeGeometry
{
public:
    ExecutableNodeGeometry(AbstractGraphModel &graphModel);

    // Geometry constants
    static constexpr int EAR_WIDTH_AUTOMATIC = 32;
    static constexpr int EAR_WIDTH_MANUAL   = 54;
    static constexpr int EAR_WIDTH_RIGHT     = 32;
    static constexpr int EAR_HEIGHT     = 28;
    static constexpr int PROGRESS_BAR_HEIGHT = 2;
    static constexpr int PROGRESS_BAR_MARGIN = 2;
    static constexpr int PROGRESS_BAR_HORIZONTAL_MARGIN = 3;
    static constexpr int EAR_OFFSET     = 30;

    QSize size(NodeId const nodeId) const override;
    void recomputeSize(NodeId const nodeId) const override;
    QPointF widgetPosition(NodeId const nodeId) const override;
    QRectF boundingRect(NodeId const nodeId) const override;

    // Get various region rectangles in node coordinates
    QRectF leftEarRect(NodeId const nodeId) const;
    QRectF rightEarRect(NodeId const nodeId) const;
    QRectF progressBarRect(NodeId const nodeId) const;

    // Hit testing for interactive elements
    bool hitTestModeButton(NodeId const nodeId, QPointF const point) const;
    bool hitTestStartButton(NodeId const nodeId, QPointF const point) const;
    bool hitTestDetailButton(NodeId const nodeId, QPointF const point) const;

private:
    // Helper method to get executable delegate model
    ExecutableNodeDelegateModel* getExecutableDelegate(NodeId const nodeId) const;
};

} // namespace QtNodes
