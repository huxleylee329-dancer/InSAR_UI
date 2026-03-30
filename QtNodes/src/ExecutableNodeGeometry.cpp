#include "ExecutableNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"
#include "DataFlowGraphModel.hpp"
#include "ExecutableNodeDelegateModel.hpp"

#include <QSize>
#include <QWidget>
#include <QSizePolicy>

namespace QtNodes {

ExecutableNodeDelegateModel* ExecutableNodeGeometry::getExecutableDelegate(NodeId const nodeId) const
{
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return nullptr;
    }
    return dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);
}

ExecutableNodeGeometry::ExecutableNodeGeometry(AbstractGraphModel &graphModel)
    : DefaultHorizontalNodeGeometry(graphModel)
{
}

QSize ExecutableNodeGeometry::size(NodeId const nodeId) const
{
    QSize baseSize = DefaultHorizontalNodeGeometry::size(nodeId);
    // Add progress bar height plus bottom margin to the total height
    return QSize(baseSize.width(), baseSize.height() + PROGRESS_BAR_HEIGHT + PROGRESS_BAR_MARGIN);
}

void ExecutableNodeGeometry::recomputeSize(NodeId const nodeId) const
{
    // Let base class do most of the work, we override size() above
    DefaultHorizontalNodeGeometry::recomputeSize(nodeId);
}

QPointF ExecutableNodeGeometry::widgetPosition(NodeId const nodeId) const
{
    // Get base position from parent class
    // Ears are drawn in the negative y region (above the main node area), so no need to shift widget down.
    // The main node content (caption, ports, widget) all start from y=0 which is already correct.
    // We only need to adjust for the extra PROGRESS_BAR_HEIGHT added at the bottom.

    QWidget *widget = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget);
    if (!widget) {
        return DefaultHorizontalNodeGeometry::widgetPosition(nodeId);
    }

    unsigned int captionHeight = captionRect(nodeId).height();
    QSize baseSize = DefaultHorizontalNodeGeometry::size(nodeId);

    // Base height stored in graph model doesn't include PROGRESS_BAR_HEIGHT at bottom.
    // The widget should be centered in the base area (excluding progress bar), same as before.
    // But since total height increased by PROGRESS_BAR_HEIGHT, we need to adjust the centering.
    double newY = (captionHeight + baseSize.height() - widget->height()) / 2.0;

    // Get base X position from parent class (x is correct)
    QPointF basePos = DefaultHorizontalNodeGeometry::widgetPosition(nodeId);

    // No extra shift needed - ears are drawn in negative y above main area, don't affect widget position
    return QPointF(basePos.x(), newY);
}

QRectF ExecutableNodeGeometry::boundingRect(NodeId const nodeId) const
{
    // Use base class boundingRect but adjust for ears
    QRectF base = AbstractNodeGeometry::boundingRect(nodeId);
    // Adjust to include ears at top (negative y)
    // and progress bar with margin at bottom
    return base.adjusted(0, -EAR_OFFSET, 0, PROGRESS_BAR_HEIGHT + PROGRESS_BAR_MARGIN);
}

QRectF ExecutableNodeGeometry::leftEarRect(NodeId const nodeId) const
{
    // Get current execution mode to determine dynamic width
    int earWidth = EAR_WIDTH_AUTOMATIC;
    auto *delegateModel = getExecutableDelegate(nodeId);
    if (delegateModel != nullptr && delegateModel->executionMode() == ExecutionMode::Manual) {
        earWidth = EAR_WIDTH_MANUAL;
    }

    // Left ear is at top-left, protruding upward by EAR_OFFSET
    double x = 0;
    double y = -EAR_OFFSET;
    double width = earWidth;
    double height = EAR_HEIGHT;
    return QRectF(x, y, width, height);
}

QRectF ExecutableNodeGeometry::rightEarRect(NodeId const nodeId) const
{
    QSize nodeSize = size(nodeId);
    // Right ear is always fixed width at top-right, protruding upward by EAR_OFFSET
    double x = nodeSize.width() - EAR_WIDTH_RIGHT;
    double y = -EAR_OFFSET;
    double width = EAR_WIDTH_RIGHT;
    double height = EAR_HEIGHT;
    return QRectF(x, y, width, height);
}

QRectF ExecutableNodeGeometry::progressBarRect(NodeId const nodeId) const
{
    QSize nodeSize = DefaultHorizontalNodeGeometry::size(nodeId);
    // Progress bar is at the bottom of the main rectangle, leave margin at bottom
    // Shrink horizontally to match node's rounded corners
    double x = PROGRESS_BAR_HORIZONTAL_MARGIN;
    double y = nodeSize.height() + PROGRESS_BAR_MARGIN;
    double width = nodeSize.width() - 2 * PROGRESS_BAR_HORIZONTAL_MARGIN;
    double height = PROGRESS_BAR_HEIGHT;
    return QRectF(x, y, width, height);
}

bool ExecutableNodeGeometry::hitTestModeButton(NodeId const nodeId, QPointF const point) const
{
    QRectF earRect = leftEarRect(nodeId);
    // Mode icon is at fixed position: left margin 4px, width 20px (icon) + 4px right padding
    // Vertical center in ear area, height 20px (same as icon size)
    double iconLeft = earRect.left() + 4;
    double iconWidth = 24;  // 20px icon + 4px right padding
    double iconTop = earRect.top() + (earRect.height() - 20) / 2.0;
    QRectF modeRect(iconLeft, iconTop, iconWidth, 20);
    return modeRect.contains(point);
}

bool ExecutableNodeGeometry::hitTestStartButton(NodeId const nodeId, QPointF const point) const
{
    // Only respond to button clicks in Manual mode
    auto *delegateModel = getExecutableDelegate(nodeId);
    if (delegateModel == nullptr || delegateModel->executionMode() != ExecutionMode::Manual) {
        return false;
    }

    QRectF earRect = leftEarRect(nodeId);
    // Button area matches drawing code: left margin 22px, top 3px, right margin 3px, bottom 3px
    double buttonX = earRect.left() + earRect.width() - 22;
    double buttonY = earRect.top() + 3;
    double buttonWidth = 19;  // 22 - 3
    double buttonHeight = earRect.height() - 6;  // 3 top + 3 bottom
    QRectF buttonRect(buttonX, buttonY, buttonWidth, buttonHeight);
    return buttonRect.contains(point);
}

bool ExecutableNodeGeometry::hitTestDetailButton(NodeId const nodeId, QPointF const point) const
{
    QRectF earRect = rightEarRect(nodeId);
    // Eye icon is centered in the right ear (20x20 icon)
    double iconSize = 20;
    double iconX = earRect.left() + (earRect.width() - iconSize) / 2.0;
    double iconY = earRect.top() + (earRect.height() - iconSize) / 2.0;
    QRectF detailRect(iconX, iconY, iconSize, iconSize);
    return detailRect.contains(point);
}

} // namespace QtNodes
