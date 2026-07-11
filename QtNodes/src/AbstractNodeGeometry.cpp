#include "AbstractNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"
#include "StyleCollection.hpp"

#include <QMargins>

#include <cmath>

namespace QtNodes {

AbstractNodeGeometry::AbstractNodeGeometry(AbstractGraphModel &graphModel)
    : _graphModel(graphModel)
{
    //
}

QRectF AbstractNodeGeometry::boundingRect(NodeId const nodeId) const
{
    QSize s = size(nodeId);

    /**
   * The node's size plus some additional margin around it to account for drawing
   * effects (for example shadows) or node's parts outside the size rectangle
   * (for example port points).
   *
   * The default implementation returns QSize + 20 percent of width and heights
   * at each side of the rectangle.
   */
    // 修改：使用固定边距代替百分比边距，避免节点越大可选区域越大的问题
    // Modified: Use fixed margin instead of percentage to avoid larger nodes having larger selectable area

    // 原代码（已注释，保留以便恢复）：
    // Original code (commented out, kept for restoration):
    /*
    double ratio = 0.20;

    int widthMargin = s.width() * ratio;
    int heightMargin = s.height() * ratio;

    QMargins margins(widthMargin, heightMargin, widthMargin, heightMargin);
    */

    // 新代码：使用固定 20 像素边距（确保能完全包含端口的圆形感应区域，避免 shape() 范围超出 boundingRect() 导致在其外部边缘悬浮时出现错误的移动画布手形光标）
    // New code: Use fixed 20 pixel margin to ensure it fully encloses port interaction radius, preventing incorrect hand cursor rendering
    int fixedMargin = 20;
    QMargins margins(fixedMargin, fixedMargin, fixedMargin, fixedMargin);

    QRectF r(QPointF(0, 0), s);

    return r.marginsAdded(margins);
}

QPointF AbstractNodeGeometry::portScenePosition(NodeId const nodeId,
                                                PortType const portType,
                                                PortIndex const index,
                                                QTransform const &t) const
{
    QPointF result = portPosition(nodeId, portType, index);

    return t.map(result);
}

PortIndex AbstractNodeGeometry::checkPortHit(NodeId const nodeId,
                                             PortType const portType,
                                             QPointF const nodePoint) const
{
    auto const &nodeStyle = StyleCollection::nodeStyle();

    PortIndex result = InvalidPortIndex;

    if (portType == PortType::None)
        return result;

    double const tolerance = 2.0 * nodeStyle.ConnectionPointDiameter;

    size_t const n = _graphModel.nodeData<unsigned int>(nodeId,
                                                        (portType == PortType::Out)
                                                            ? NodeRole::OutPortCount
                                                            : NodeRole::InPortCount);

    for (unsigned int portIndex = 0; portIndex < n; ++portIndex) {
        auto pp = portPosition(nodeId, portType, portIndex);

        QPointF p = pp - nodePoint;
        auto distance = std::sqrt(QPointF::dotProduct(p, p));

        if (distance < tolerance) {
            result = portIndex;
            break;
        }
    }

    return result;
}

} // namespace QtNodes
