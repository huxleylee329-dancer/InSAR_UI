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
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return DefaultHorizontalNodeGeometry::size(nodeId);
    }
    auto *execModel = dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);

    QSize contentSize = DefaultHorizontalNodeGeometry::size(nodeId);

    if (execModel && !execModel->useExternalLayout()) {
        // Card layout: header + content + footer
        // Subtract base class caption height and spacing since we don't use them in card layout
        QRectF baseCapRect = DefaultHorizontalNodeGeometry::captionRect(nodeId);
        // _portSpasing is 10 in base class, subtract 10*2 = 20
        int adjustedContentHeight = contentSize.height() - baseCapRect.height() - 10;
        
        int totalHeight = CARD_HEADER_HEIGHT + CARD_MARGIN * 2 +
                          adjustedContentHeight + CARD_FOOTER_HEIGHT;
        int totalWidth = contentSize.width() + CARD_MARGIN * 2;
        return QSize(totalWidth, totalHeight);
    }

    // Original layout: content + progress bar
    // contentSize already includes caption and ports, just add progress bar
    int const height = contentSize.height() + PROGRESS_BAR_HEIGHT + PROGRESS_BAR_MARGIN;
    return QSize(contentSize.width(), height);
}

void ExecutableNodeGeometry::recomputeSize(NodeId const nodeId) const
{
    // Let base class do most of the work, we override size() above
    DefaultHorizontalNodeGeometry::recomputeSize(nodeId);
}

QPointF ExecutableNodeGeometry::widgetPosition(NodeId const nodeId) const
{
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return DefaultHorizontalNodeGeometry::widgetPosition(nodeId);
    }
    auto *execModel = dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);

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

    double newY;

    if (execModel && !execModel->useExternalLayout()) {
        // Card layout: widget starts after header + margin (top)
        newY = CARD_HEADER_HEIGHT + CARD_MARGIN;
    } else {
        // Original layout: Base height stored in graph model doesn't include PROGRESS_BAR_HEIGHT at bottom.
        // The widget should be centered in the base area (excluding progress bar), same as before.
        // But since total height increased by PROGRESS_BAR_HEIGHT, we need to adjust the centering.
        newY = (captionHeight + baseSize.height() - widget->height()) / 2.0;
    }

    // Get base X position from parent class (x is correct)
    QPointF basePos = DefaultHorizontalNodeGeometry::widgetPosition(nodeId);

    // For card layout, also shift X by CARD_MARGIN
    if (execModel && !execModel->useExternalLayout()) {
        return QPointF(basePos.x() + CARD_MARGIN, newY);
    }

    return QPointF(basePos.x(), newY);
}

QPointF ExecutableNodeGeometry::portPosition(NodeId const nodeId,
                                              PortType const portType,
                                              PortIndex const index) const
{
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return DefaultHorizontalNodeGeometry::portPosition(nodeId, portType, index);
    }
    auto *execModel = dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);

    if (execModel && !execModel->useExternalLayout()) {
        // Card layout: compute port position from scratch, no recursion!
        unsigned int const step = 20 + 10; // portSize + portSpasing from base class
        
        double totalY = CARD_HEADER_HEIGHT + CARD_MARGIN; // start after header
        totalY += step * index;
        totalY += step / 2.0;

        QSize size = this->size(nodeId);

        double x;
        switch (portType) {
        case PortType::In:
            x = CARD_MARGIN;
            break;
        case PortType::Out:
            x = size.width() - CARD_MARGIN;
            break;
        default:
            x = 0;
            break;
        }
        return QPointF(x, totalY);
    }

    return DefaultHorizontalNodeGeometry::portPosition(nodeId, portType, index);
}

QPointF ExecutableNodeGeometry::portTextPosition(NodeId const nodeId,
                                                 PortType const portType,
                                                 PortIndex const index) const
{
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return DefaultHorizontalNodeGeometry::portTextPosition(nodeId, portType, index);
    }
    auto *execModel = dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);

    if (execModel && !execModel->useExternalLayout()) {
        // Card layout: compute based on our own portPosition, no recursion!
        QPointF p = portPosition(nodeId, portType, index);
        
        // Get the text rect
        QString s;
        if (_graphModel.portData<bool>(nodeId, portType, index, PortRole::CaptionVisible)) {
            s = _graphModel.portData<QString>(nodeId, portType, index, PortRole::Caption);
        } else {
            auto portData = _graphModel.portData(nodeId, portType, index, PortRole::DataType);
            s = portData.value<NodeDataType>().name;
        }
        QFont f;
        QFontMetrics fm(f);
        QRectF rect = fm.boundingRect(s);
        
        // Apply same logic as base class: add rect.height()/4 to y
        p.setY(p.y() + rect.height() / 4.0);
        
        // Adjust x position
        QSize size = this->size(nodeId);
        switch (portType) {
        case PortType::In:
            p.setX(CARD_MARGIN + 10); // _portSpasing is 10
            break;
        case PortType::Out:
            p.setX(size.width() - CARD_MARGIN - 10 - rect.width()); // _portSpasing is 10
            break;
        default:
            break;
        }
        return p;
    }

    return DefaultHorizontalNodeGeometry::portTextPosition(nodeId, portType, index);
}

QRectF ExecutableNodeGeometry::captionRect(NodeId const nodeId) const
{
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return DefaultHorizontalNodeGeometry::captionRect(nodeId);
    }
    auto *execModel = dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);

    if (execModel && !execModel->useExternalLayout()) {
        // Card layout: caption is drawn in header by painter, not by default painter
        return QRect();
    }

    return DefaultHorizontalNodeGeometry::captionRect(nodeId);
}

QPointF ExecutableNodeGeometry::captionPosition(NodeId const nodeId) const
{
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
    if (dfModel == nullptr) {
        return DefaultHorizontalNodeGeometry::captionPosition(nodeId);
    }
    auto *execModel = dfModel->delegateModel<ExecutableNodeDelegateModel>(nodeId);

    QPointF basePos = DefaultHorizontalNodeGeometry::captionPosition(nodeId);

    if (execModel && !execModel->useExternalLayout()) {
        // Card layout: caption is drawn in header by painter
        return QPointF(basePos.x(), basePos.y() + CARD_HEADER_HEIGHT);
    }

    return basePos;
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
    // Always use the wider ear width to fit both the mode icon and the start/stop button
    int earWidth = EAR_WIDTH_MANUAL;

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
    auto *delegateModel = getExecutableDelegate(nodeId);
    if (delegateModel == nullptr) {
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

bool ExecutableNodeGeometry::hitTestCardModeButton(NodeId const nodeId, QPointF const point) const
{
    // Card header: mode button on the left
    QSize nodeSize = size(nodeId);
    QRectF headerRect(0, 0, nodeSize.width(), CARD_HEADER_HEIGHT);
    // Icon is 20x20, left margin 4
    QRectF modeRect(4, headerRect.top() + (headerRect.height() - 20) / 2.0, 20, 20);
    return modeRect.contains(point);
}

bool ExecutableNodeGeometry::hitTestCardStartButton(NodeId const nodeId, QPointF const point) const
{
    auto *delegateModel = getExecutableDelegate(nodeId);
    if (delegateModel == nullptr) {
        return false;
    }
    
    QSize nodeSize = size(nodeId);
    QRectF headerRect(0, 0, nodeSize.width(), CARD_HEADER_HEIGHT);
    // Start button is to the left of detail button (eye) - new spacing
    // Detail button (eye) is 20x20, right margin 12, add gap 16
    double buttonSize = 20;
    double detailX = headerRect.right() - 12 - buttonSize;
    double startX = detailX - 16 - buttonSize;
    QRectF startRect(startX, headerRect.top() + (headerRect.height() - buttonSize) / 2.0, buttonSize, buttonSize);
    return startRect.contains(point);
}

bool ExecutableNodeGeometry::hitTestCardDetailButton(NodeId const nodeId, QPointF const point) const
{
    QSize nodeSize = size(nodeId);
    QRectF headerRect(0, 0, nodeSize.width(), CARD_HEADER_HEIGHT);
    // Eye icon is 20x20, right margin 12 (updated spacing)
    double iconSize = 20;
    double iconX = headerRect.right() - 12 - iconSize;
    double iconY = headerRect.top() + (headerRect.height() - iconSize) / 2.0;
    QRectF detailRect(iconX, iconY, iconSize, iconSize);
    return detailRect.contains(point);
}

} // namespace QtNodes
