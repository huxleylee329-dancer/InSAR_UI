#include "ExecutableNodePainter.hpp"

#include "NodeGraphicsObject.hpp"
#include "BasicGraphicsScene.hpp"
#include "DefaultNodePainter.hpp"
#include "NodeStyle.hpp"
#include "StyleCollection.hpp"
#include "DataFlowGraphModel.hpp"

#include <QPainter>
#include <QStyle>
#include <QPixmap>
#include <QImage>
#include <QDebug>

#include <QtSvg/QSvgRenderer>

// Initialize static constexpr color constants
namespace {
    // Icon colors
    constexpr QColor COLOR_AUTOMATIC{0, 120, 215};  // #0078D7
    constexpr QColor COLOR_MANUAL{216, 59, 1};      // #D83B01
    constexpr QColor COLOR_PLAY{0, 160, 0};         // #00A000
    constexpr QColor COLOR_STOP{200, 50, 50};       // #C83232
    constexpr QColor COLOR_EYE{200, 200, 200};      // #C8C8C8

    // State background colors (VIBRANT VERSION)
    constexpr QColor COLOR_IDLE_START{245, 245, 245};      // #F5F5F5 (unchanged - idle should be subtle)
    constexpr QColor COLOR_IDLE_END{232, 232, 232};         // #E8E8E8 (unchanged)
    constexpr QColor COLOR_PENDING{209, 196, 233};         // #D1C4E9 (vibrant purple)
    constexpr QColor COLOR_RUNNING_START{30, 136, 229};     // #1E88E5 (vibrant blue)
    constexpr QColor COLOR_RUNNING_END{61, 168, 240};       // #3DA8F0 (vibrant blue)
    constexpr QColor COLOR_COMPLETED{0, 230, 118};          // #00E676 (vibrant green)
    constexpr QColor COLOR_STOPPED_START{255, 159, 67};    // #FF9F43 (vibrant orange)
    constexpr QColor COLOR_STOPPED_END{255, 109, 0};        // #FF6D00 (vibrant orange)
    constexpr QColor COLOR_WARNING_START{255, 214, 0};      // #FFD600 (vibrant yellow)
    constexpr QColor COLOR_WARNING_END{255, 193, 7};         // #FFC107 (vibrant yellow)
    constexpr QColor COLOR_ERROR_START{255, 23, 68};        // #FF1744 (vibrant red)
    constexpr QColor COLOR_ERROR_END{213, 0, 0};            // #D50000 (vibrant red)
    constexpr QColor COLOR_DISABLED{200, 200, 200};       // #C8C8C8 (darker gray for disabled)
}

namespace QtNodes {

QPixmap ExecutableNodePainter::loadAndColorizeIcon(const QString &resourcePath, const QColor &color, const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        qWarning() << "Failed to load SVG icon:" << resourcePath;
        return QPixmap();
    }

    QPainter painter(&image);
    renderer.render(&painter, image.rect());

    // Recolor all non-transparent pixels
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor pixelColor = image.pixelColor(x, y);
            if (pixelColor.alpha() > 0) {
                pixelColor.setRgb(color.red(), color.green(), color.blue(), pixelColor.alpha());
                image.setPixel(x, y, pixelColor.rgba());
            }
        }
    }

    return QPixmap::fromImage(image);
}

ExecutableNodePainter::ExecutableNodePainter()
{
    // Load and colorize SVG icons from resources
    _pixmapAutomatic = loadAndColorizeIcon(QStringLiteral(":/QtWidgetsApplication3/refresh-cw.svg"), ::COLOR_AUTOMATIC, QSize(20, 20));
    _pixmapManual = loadAndColorizeIcon(QStringLiteral(":/QtWidgetsApplication3/refresh-cw-off.svg"), ::COLOR_MANUAL, QSize(20, 20));
    _pixmapPlay = loadAndColorizeIcon(QStringLiteral(":/QtWidgetsApplication3/play.svg"), ::COLOR_PLAY, QSize(16, 16));
    _pixmapStop = loadAndColorizeIcon(QStringLiteral(":/QtWidgetsApplication3/stop.svg"), ::COLOR_STOP, QSize(16, 16));
    _pixmapEye = loadAndColorizeIcon(QStringLiteral(":/QtWidgetsApplication3/eye.svg"), ::COLOR_EYE, QSize(20, 20));
}

void ExecutableNodePainter::paint(QPainter *painter, NodeGraphicsObject &ngo) const
{
    auto &graphModel = ngo.graphModel();
    NodeId nodeId = ngo.nodeId();

    // Check if this is a data flow graph model
    auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&graphModel);
    if (!dfModel) {
        _defaultPainter.paint(painter, ngo);
        return;
    }

    auto *delegateModel = dfModel->delegateModel<NodeDelegateModel>(nodeId);
    if (!delegateModel) {
        _defaultPainter.paint(painter, ngo);
        return;
    }

    // Check if this is an executable node using external layout
    auto *execModel = dynamic_cast<ExecutableNodeDelegateModel*>(delegateModel);
    if (!execModel || !execModel->useExternalLayout()) {
        // Fall back to default painting for non-executable nodes
        _defaultPainter.paint(painter, ngo);
        return;
    }

    auto &scene = *ngo.nodeScene();
    auto &geo = dynamic_cast<ExecutableNodeGeometry&>(scene.nodeGeometry());

    ExecutionMode mode = execModel->executionMode();
    ExecutionState state = execModel->executionState();
    int progress = execModel->progress();
    bool isSelected = ngo.isSelected();

    // Antialiasing for smoother drawing
    painter->setRenderHint(QPainter::Antialiasing);

    // Draw main node rectangle
    drawMainRect(painter, ngo, geo, mode, state);

    // Draw text background for better readability on all states
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 70)); // Semi-transparent dark (increased alpha for better contrast)

        AbstractGraphModel &model = ngo.graphModel();
        auto &geometry = ngo.nodeScene()->nodeGeometry();

        if (model.nodeData(nodeId, NodeRole::CaptionVisible).toBool()) {
            QString const name = model.nodeData(nodeId, NodeRole::Caption).toString();

            // Get the baseline position for caption text (same as what drawNodeCaption uses)
            QPointF baseline = geometry.captionPosition(nodeId);

            // Use bold font to get correct metrics (same as drawNodeCaption uses bold)
            QFont f = painter->font();
            f.setBold(true);
            QFontMetrics fm(f);

            // Calculate bounding rect and adjust position for baseline
            QRectF bounds = fm.boundingRect(name);
            double ascent = fm.ascent();
            QPointF topLeft(baseline.x(), baseline.y() - ascent);
            bounds.moveTopLeft(topLeft);

            // Draw background rectangle
            painter->drawRoundedRect(bounds.adjusted(-2, -2, 2, 2), 2.0, 2.0);
        }
    }

    // Draw the standard node elements (caption, ports, etc.)
    _defaultPainter.drawNodeCaption(painter, ngo);

    // Draw background for port entry labels before drawing text
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 55));

        AbstractGraphModel &model = ngo.graphModel();
        AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();
        QFontMetrics fm(painter->font());

        for (PortType portType : {PortType::Out, PortType::In}) {
            unsigned int n = model.nodeData<unsigned int>(nodeId,
                                  (portType == PortType::Out)
                                      ? NodeRole::OutPortCount
                                      : NodeRole::InPortCount);

            for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
                QString s;

                if (model.portData<bool>(nodeId, portType, portIndex, PortRole::CaptionVisible)) {
                    s = model.portData<QString>(nodeId, portType, portIndex, PortRole::Caption);
                } else {
                    auto portData = model.portData(nodeId, portType, portIndex, PortRole::DataType);
                    s = portData.value<NodeDataType>().name;
                }

                if (!s.isEmpty()) {
                    QPointF baseline = geometry.portTextPosition(nodeId, portType, portIndex);
                    QRectF bounds = fm.boundingRect(s);

                    // bounds.y() is the ascent (distance from baseline to top)
                    // We need to move topLeft up by ascent to get correct positioning
                    // because p is the baseline position, not top-left corner
                    double ascent = fm.ascent();
                    QPointF topLeft(baseline.x(), baseline.y() - ascent);
                    bounds.moveTopLeft(topLeft);

                    painter->drawRoundedRect(bounds.adjusted(-1, -1, 1, 1), 1.0, 1.0);
                }
            }
        }
    }

    _defaultPainter.drawConnectionPoints(painter, ngo);
    _defaultPainter.drawFilledConnectionPoints(painter, ngo);
    _defaultPainter.drawEntryLabels(painter, ngo);
    _defaultPainter.drawResizeRect(painter, ngo);

    // Draw progress bar - always visible
    drawProgressBar(painter, ngo, geo, progress);

    // Draw ears last so they are on top - only when selected
    if (isSelected) {
        drawLeftEar(painter, ngo, geo, mode, state);
        drawRightEar(painter, ngo, geo, state);
    }
}

void ExecutableNodePainter::drawMainRect(QPainter *painter, NodeGraphicsObject &ngo,
                                         ExecutableNodeGeometry &geo,
                                         ExecutionMode mode, ExecutionState state) const
{
    auto &nodeStyle = StyleCollection::nodeStyle();
    QSize size = geo.size(ngo.nodeId());

    // Main rectangle leaves space at bottom for progress bar and margin
    QRectF boundary(0, 0, size.width(), size.height() - geo.PROGRESS_BAR_HEIGHT - geo.PROGRESS_BAR_MARGIN);
    double const radius = 3.0;

    QPen pen(nodeStyle.SelectedBoundaryColor, 1.0);
    QPen normalPen(nodeStyle.NormalBoundaryColor, 1.0);
    painter->setPen(ngo.isSelected() ? pen : normalPen);

    // For Automatic mode + Idle state, use default NodeStyle gradient
    // Otherwise use state-based gradient colors
    QLinearGradient gradient(QPointF(0, 0), QPointF(0, boundary.height()));
    if (mode == ExecutionMode::Automatic && state == ExecutionState::Idle) {
        // Use default NodeStyle colors
        gradient.setColorAt(0.0, nodeStyle.GradientColor0);
        gradient.setColorAt(0.3, nodeStyle.GradientColor1);
        gradient.setColorAt(0.7, nodeStyle.GradientColor2);
        gradient.setColorAt(1.0, nodeStyle.GradientColor3);
    } else {
        gradient.setColorAt(0.0, gradientStartColor(mode, state));
        gradient.setColorAt(0.3, gradientStartColor(mode, state));
        gradient.setColorAt(0.7, gradientEndColor(mode, state));
        gradient.setColorAt(1.0, gradientEndColor(mode, state));
    }
    painter->setBrush(gradient);

    painter->drawRoundedRect(boundary, radius, radius);

    // Draw texture overlays
    if (state == ExecutionState::Disabled) {
        drawDisabledTexture(painter, boundary);
    } else if (mode == ExecutionMode::Manual) {
        drawManualTexture(painter, boundary);
    }
}

void ExecutableNodePainter::drawLeftEar(QPainter *painter, NodeGraphicsObject &ngo,
                                        ExecutableNodeGeometry &geo,
                                        ExecutionMode mode, ExecutionState state) const
{
    //auto &nodeStyle = StyleCollection::nodeStyle();
    QRectF earRect = geo.leftEarRect(ngo.nodeId());

    // Draw ear background with new style: dark semi-transparent background + light border
    //QPen pen(QColor(255, 255, 255, 50), 1.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(45, 45, 45, 200));

    double radius = 3.0;
    painter->drawRoundedRect(earRect, radius, radius);

    // Draw mode icon - different layout based on mode
    // In Automatic mode: entire left area is used for icon (no button reserved)
    // In Manual mode: leave space on right for start/stop button
    int rightMargin = (mode == ExecutionMode::Automatic) ? 4 : 24;
    QRectF iconArea = earRect.adjusted(8, 4, -rightMargin, -4);
    QPixmap const &pixmap = (mode == ExecutionMode::Automatic) ? _pixmapAutomatic : _pixmapManual;

    // Align to left with 4px margin, center vertically
    double left = iconArea.left();
    double top = iconArea.top() + (iconArea.height() - pixmap.height()) / 2.0;
    QPoint topLeft = QPoint(static_cast<int>(left), static_cast<int>(top));
    painter->drawPixmap(topLeft, pixmap);

    // Draw start/stop button icon - only visible in Manual mode
    if (mode == ExecutionMode::Manual) {
        QRectF buttonRect = earRect.adjusted(static_cast<int>(earRect.width()) - 22, 3, -3, -3);
        drawStartButton(painter, buttonRect, state);
    }
}

void ExecutableNodePainter::drawRightEar(QPainter *painter, NodeGraphicsObject &ngo,
                                         ExecutableNodeGeometry &geo,
                                         ExecutionState /*state*/) const
{
    QRectF earRect = geo.rightEarRect(ngo.nodeId());

    // Draw ear background with same style as left ear: dark semi-transparent background + light border
    // QPen pen(QColor(255, 255, 255, 50), 1.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(45, 45, 45, 200));

    double radius = 3.0;
    painter->drawRoundedRect(earRect, radius, radius);

    // Draw eye icon centered in the ear
    double dx = (earRect.width() - _pixmapEye.width()) / 2.0;
    double dy = (earRect.height() - _pixmapEye.height()) / 2.0;
    QPoint topLeft = QPoint(static_cast<int>(earRect.left() + dx), static_cast<int>(earRect.top() + dy));
    painter->drawPixmap(topLeft, _pixmapEye);
}

void ExecutableNodePainter::drawProgressBar(QPainter *painter, NodeGraphicsObject &ngo,
                                            ExecutableNodeGeometry &geo,
                                            int progress) const
{
    QRectF barRect = geo.progressBarRect(ngo.nodeId());

    // Adjust margin and radius based on bar height
    double margin = (barRect.height() >= 3) ? 1.0 : 0.0;
    double radius = qMin(2.0, barRect.height() / 2.0);

    // Background
    painter->setPen(QColor(204, 204, 204));
    painter->setBrush(QColor(240, 240, 240));
    painter->drawRoundedRect(barRect, radius, radius);

    // Progress chunk
    if (progress > 0) {
        double rightMargin = -(barRect.width() - progress * barRect.width() / 100 - 2 * margin);
        double bottomMargin = -margin;
        QRectF progressRect = barRect.adjusted(margin, margin, rightMargin, bottomMargin);
        // Ensure we have at least 1px width when progress > 0
        if (progressRect.width() < 1) {
            progressRect.setWidth(1);
        }
        painter->setBrush(QColor(0, 120, 212));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(progressRect, qMax(0.0, radius - margin), qMax(0.0, radius - margin));
    }
}

void ExecutableNodePainter::drawStartButton(QPainter *painter, QRectF rect, ExecutionState state) const
{
    // Draw SVG icon from pre-rendered pixmap - keep original size, center in rect
    QPixmap const &pixmap = (state == ExecutionState::Running) ? _pixmapStop : _pixmapPlay;
    // Center icon in the available area
    double dx = (rect.width() - pixmap.width()) / 2.0;
    double dy = (rect.height() - pixmap.height()) / 2.0;
    QPoint topLeft = QPoint(static_cast<int>(rect.left() + dx), static_cast<int>(rect.top() + dy));
    painter->drawPixmap(topLeft, pixmap);
}

QColor ExecutableNodePainter::stateColor(ExecutionState /*state*/) const
{
    // This method is currently unused but kept for potential future use
    return QColor(136, 136, 136);
}

QColor ExecutableNodePainter::gradientStartColor(ExecutionMode mode, ExecutionState state) const
{
    Q_UNUSED(mode);
    switch (state) {
    case ExecutionState::Idle:
        return ::COLOR_IDLE_START;
    case ExecutionState::Pending:
        return ::COLOR_PENDING;
    case ExecutionState::Running:
        return ::COLOR_RUNNING_START;
    case ExecutionState::Completed:
        return ::COLOR_COMPLETED;
    case ExecutionState::Stopped:
        return ::COLOR_STOPPED_START;
    case ExecutionState::Warning:
        return ::COLOR_WARNING_START;
    case ExecutionState::Error:
        return ::COLOR_ERROR_START;
    case ExecutionState::Disabled:
        return ::COLOR_DISABLED;
    }
    return ::COLOR_IDLE_START;
}

QColor ExecutableNodePainter::gradientEndColor(ExecutionMode mode, ExecutionState state) const
{
    Q_UNUSED(mode);
    switch (state) {
    case ExecutionState::Idle:
        return ::COLOR_IDLE_END;
    case ExecutionState::Pending:
        return ::COLOR_PENDING;
    case ExecutionState::Running:
        return ::COLOR_RUNNING_END;
    case ExecutionState::Completed:
        return ::COLOR_COMPLETED;
    case ExecutionState::Stopped:
        return ::COLOR_STOPPED_END;
    case ExecutionState::Warning:
        return ::COLOR_WARNING_END;
    case ExecutionState::Error:
        return ::COLOR_ERROR_END;
    case ExecutionState::Disabled:
        return ::COLOR_DISABLED;
    }
    return ::COLOR_IDLE_END;
}

void ExecutableNodePainter::drawManualTexture(QPainter *painter, const QRectF &rect) const
{
    // Draw diagonal line texture for Manual mode
    // Line color: semi-transparent dark gray
    // Line angle: 45 degrees
    // Line spacing: 5 pixels
    // Line width: 1 pixel

    painter->save();
    painter->setClipRect(rect);

    QPen texturePen(QColor(0, 0, 0, 100), 1); // Semi-transparent dark gray for visibility
    painter->setPen(texturePen);
    painter->setBrush(QBrush(Qt::black));

    // Draw diagonal lines from top-right to bottom-left (45 degree angle)
    double spacing = 10.0;  // Spaced out more for less dense texture
    double xStart = rect.left();

    // Lines starting from top edge
    for (double x = xStart; x < rect.right() + rect.height(); x += spacing) {
        double y1 = rect.top();
        double x1 = x;
        double y2 = rect.bottom();
        double x2 = x - rect.height();
        painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }

    painter->restore();
}

void ExecutableNodePainter::drawDisabledTexture(QPainter *painter, const QRectF &rect) const
{
    // Draw denser diagonal line texture for Disabled state
    // Same as Manual texture but with denser lines

    painter->save();
    painter->setClipRect(rect);

    QPen texturePen(QColor(200, 200, 200, 100), 1); // Denser, darker lines
    painter->setPen(texturePen);
    painter->setBrush(Qt::NoBrush);

    // Draw diagonal lines with 3 pixel spacing
    double spacing = 3.0;
    double xStart = rect.left();

    // Lines starting from top edge
    for (double x = xStart; x < rect.right() + rect.height(); x += spacing) {
        double y1 = rect.top();
        double x1 = x;
        double y2 = rect.bottom();
        double x2 = x - rect.height();
        painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }

    painter->restore();
}

} // namespace QtNodes
