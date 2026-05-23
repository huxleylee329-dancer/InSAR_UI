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

// Initialize color scheme (Dynamic Theme Adaptation) following ui2.md
namespace {
    // Icon colors (ui2.md Section 122-131)
    constexpr QColor COLOR_AUTOMATIC{0, 95, 172};       // #005FAC (main theme color)
    constexpr QColor COLOR_MANUAL{153, 71, 0};          // #994700 (tertiary color)
    constexpr QColor COLOR_PLAY{16, 185, 129};          // #10B981 (play button)
    constexpr QColor COLOR_STOP{239, 68, 68};           // #EF4444 (stop button)
    constexpr QColor COLOR_EYE{148, 163, 184};          // #94A3B8 (eye icon)

    // State background colors - LIGHT THEME (ui2.md Section 87-97)
    constexpr QColor COLOR_IDLE_START_L{249, 249, 249}; // #F9F9F9 (main background)
    constexpr QColor COLOR_IDLE_END_L{233, 233, 233};   // #E9E9E9
    constexpr QColor COLOR_PENDING_L{0, 95, 172};       // #005FAC (ready/running)
    constexpr QColor COLOR_RUNNING_START_L{0, 95, 172}; // #005FAC
    constexpr QColor COLOR_RUNNING_END_L{0, 120, 215};  // #0078D7 (hover)
    constexpr QColor COLOR_COMPLETED_L{16, 185, 129};   // #10B981
    constexpr QColor COLOR_STOPPED_START_L{245, 158, 11}; // #F59E0B
    constexpr QColor COLOR_STOPPED_END_L{245, 158, 11};   // #F59E0B
    constexpr QColor COLOR_WARNING_START_L{251, 191, 36}; // #FBBF24
    constexpr QColor COLOR_WARNING_END_L{251, 191, 36};   // #FBBF24
    constexpr QColor COLOR_ERROR_START_L{239, 68, 68};    // #EF4444
    constexpr QColor COLOR_ERROR_END_L{239, 68, 68};      // #EF4444
    constexpr QColor COLOR_DISABLED_L{148, 163, 184};   // #94A3B8 (grayscale + opacity)

    // State background colors - DARK THEME (ui2.md Section 102-110)
    constexpr QColor COLOR_IDLE_START_D{26, 28, 28};    // #1A1C1C (main background)
    constexpr QColor COLOR_IDLE_END_D{43, 43, 43};      // #2B2B2B
    constexpr QColor COLOR_PENDING_D{0, 120, 215};      // #0078D7 (ready/running)
    constexpr QColor COLOR_RUNNING_START_D{0, 120, 215};// #0078D7
    constexpr QColor COLOR_RUNNING_END_D{0, 120, 215};  // #0078D7
    constexpr QColor COLOR_COMPLETED_D{74, 169, 207};   // #4AA9CF
    constexpr QColor COLOR_STOPPED_START_D{251, 191, 36}; // #FBBF24
    constexpr QColor COLOR_STOPPED_END_D{251, 191, 36};   // #FBBF24
    constexpr QColor COLOR_WARNING_START_D{251, 191, 36}; // #FBBF24
    constexpr QColor COLOR_WARNING_END_D{251, 191, 36};   // #FBBF24
    constexpr QColor COLOR_ERROR_START_D{239, 68, 68};    // #EF4444
    constexpr QColor COLOR_ERROR_END_D{239, 68, 68};      // #EF4444
    constexpr QColor COLOR_DISABLED_D{80, 80, 80};      // #505050 (grayscale + opacity)
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
    _pixmapAutomatic = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/refresh-cw.svg"), ::COLOR_AUTOMATIC, QSize(20, 20));
    _pixmapManual = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/refresh-cw-off.svg"), ::COLOR_MANUAL, QSize(20, 20));
    _pixmapPlay = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/play.svg"), ::COLOR_PLAY, QSize(16, 16));
    _pixmapStop = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/stop.svg"), ::COLOR_STOP, QSize(16, 16));
    _pixmapEye = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/eye.svg"), ::COLOR_EYE, QSize(20, 20));

    // Load state icons for card footer (18x18)
    _pixmapStateIdle = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/pause.svg"), QColor(113, 119, 132), QSize(18, 18));
    _pixmapStatePending = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/hourglass.svg"), QColor(59, 130, 246), QSize(18, 18));
    _pixmapStateRunning = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/sync.svg"), QColor(59, 130, 246), QSize(18, 18));
    _pixmapStateCompleted = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/check-circle.svg"), QColor(16, 185, 129), QSize(18, 18));
    _pixmapStateStopped = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/stop.svg"), QColor(245, 158, 11), QSize(18, 18));
    _pixmapStateWarning = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/warning.svg"), QColor(245, 158, 11), QSize(18, 18));
    _pixmapStateError = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/x-circle.svg"), QColor(239, 68, 68), QSize(18, 18));
    _pixmapStateDisabled = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/svg/block.svg"), QColor(148, 163, 184), QSize(18, 18));
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

    // Check if this is an executable node
    auto *execModel = dynamic_cast<ExecutableNodeDelegateModel*>(delegateModel);
    if (!execModel) {
        // Fall back to default painting for non-executable nodes
        _defaultPainter.paint(painter, ngo);
        return;
    }

    if (!execModel->useExternalLayout()) {
        // Use card-based layout
        drawCardLayout(painter, ngo, execModel);
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
    // Get the view widget from the scene for theme detection context
    ::QWidget* context = nullptr;
    if (!ngo.nodeScene()->views().isEmpty()) {
        QGraphicsView* view = ngo.nodeScene()->views().first();
        context = (QWidget*)view;
    }
    drawMainRect(painter, ngo, geo, mode, state, context);  // Pass view as context for theme detection

    // Draw "M" badge for Manual mode in node body (top-left corner)
    if (mode == ExecutionMode::Manual) {
        painter->save();

        // Badge position: top-left corner of main rectangle
        // Offset 4px from edges for better visibility
        QRectF badgeRect(8, 4, 16, 16);

        // Draw badge background
        painter->setPen(Qt::NoPen);
        painter->setBrush(::COLOR_MANUAL);
        painter->drawRoundedRect(badgeRect, 3.0, 3.0);

        // Draw "M" text
        painter->setPen(Qt::white);
        QFont badgeFont = painter->font();
        badgeFont.setBold(false);
        badgeFont.setPointSize(9);
        painter->setFont(badgeFont);
        painter->drawText(badgeRect, Qt::AlignCenter, "M");

        painter->restore();
    }

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
    drawProgressBar(painter, ngo, geo, progress, context);

    // Draw ears last so they are on top - only when selected
    if (isSelected) {
        drawLeftEar(painter, ngo, geo, mode, state, context);
        drawRightEar(painter, ngo, geo, state, context);
    }
}

void ExecutableNodePainter::drawMainRect(QPainter *painter, NodeGraphicsObject &ngo,
                                         ExecutableNodeGeometry &geo,
                                         ExecutionMode mode, ExecutionState state,
                                         QWidget* context) const
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
    // Otherwise use state-based gradient colors (theme-aware)
    QLinearGradient gradient(QPointF(0, 0), QPointF(0, boundary.height()));
    if (mode == ExecutionMode::Automatic && state == ExecutionState::Idle) {
        // Use default NodeStyle colors
        gradient.setColorAt(0.0, nodeStyle.GradientColor0);
        gradient.setColorAt(0.3, nodeStyle.GradientColor1);
        gradient.setColorAt(0.7, nodeStyle.GradientColor2);
        gradient.setColorAt(1.0, nodeStyle.GradientColor3);
    } else {
        // Use theme-aware colors
        gradient.setColorAt(0.0, gradientStartColor(mode, state, context));
        gradient.setColorAt(0.3, gradientStartColor(mode, state, context));
        gradient.setColorAt(0.7, gradientEndColor(mode, state, context));
        gradient.setColorAt(1.0, gradientEndColor(mode, state, context));
    }
    painter->setBrush(gradient);

    painter->drawRoundedRect(boundary, radius, radius);

    // Apply opacity for Disabled state (UX best practice: use opacity reduction instead of texture)
    if (state == ExecutionState::Disabled) {
        painter->save();
        painter->setOpacity(0.5);  // 50% opacity for disabled state
        painter->drawRoundedRect(boundary, radius, radius);
        painter->restore();
    }
}

void ExecutableNodePainter::drawLeftEar(QPainter *painter, NodeGraphicsObject &ngo,
                                        ExecutableNodeGeometry &geo,
                                        ExecutionMode mode, ExecutionState state,
                                        QWidget* context) const
{
    //auto &nodeStyle = StyleCollection::nodeStyle();
    QRectF earRect = geo.leftEarRect(ngo.nodeId());

    // Draw ear background with new style: dark semi-transparent background + light border
    //QPen pen(QColor(255, 255, 255, 50), 1.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(45, 45, 45, 200));

    double radius = 3.0;
    painter->drawRoundedRect(earRect, radius, radius);

    // Always leave space on right for start/stop button in all modes
    int rightMargin = 24;
    QRectF iconArea = earRect.adjusted(8, 4, -rightMargin, -4);
    QPixmap const &pixmap = (mode == ExecutionMode::Automatic) ? _pixmapAutomatic : _pixmapManual;

    // Align to left with 4px margin, center vertically
    double left = iconArea.left();
    double top = iconArea.top() + (iconArea.height() - pixmap.height()) / 2.0;
    QPoint topLeft = QPoint(static_cast<int>(left), static_cast<int>(top));
    painter->drawPixmap(topLeft, pixmap);

    // Draw start/stop button icon - always visible
    QRectF buttonRect = earRect.adjusted(static_cast<int>(earRect.width()) - 22, 3, -3, -3);
    drawStartButton(painter, buttonRect, state);
}

void ExecutableNodePainter::drawRightEar(QPainter *painter, NodeGraphicsObject &ngo,
                                         ExecutableNodeGeometry &geo,
                                         ExecutionState /*state*/,
                                         QWidget* context) const
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
                                            int progress,
                                            QWidget* context) const
{
    QRectF barRect = geo.progressBarRect(ngo.nodeId());

    // Adjust margin and radius based on bar height
    double margin = (barRect.height() >= 3) ? 1.0 : 0.0;
    double radius = qMin(2.0, barRect.height() / 2.0);

    // Background (theme-aware idle colors)
    QColor idleStart = themedColor(::COLOR_IDLE_START_L, ::COLOR_IDLE_START_D, context);
    QColor idleEnd = themedColor(::COLOR_IDLE_END_L, ::COLOR_IDLE_END_D, context);
    painter->setPen(idleEnd);
    painter->setBrush(idleStart);
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
        // Use theme-aware running color for progress
        QColor runningStart = themedColor(::COLOR_RUNNING_START_L, ::COLOR_RUNNING_START_D, context);
        painter->setBrush(runningStart);
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

    if (state == ExecutionState::Completed) {
        painter->setOpacity(0.5);
    }
    painter->drawPixmap(topLeft, pixmap);
    if (state == ExecutionState::Completed) {
        painter->setOpacity(1.0);
    }
}

void ExecutableNodePainter::drawCardLayout(QPainter *painter, NodeGraphicsObject &ngo,
                                            ExecutableNodeDelegateModel *execModel) const
{
    auto &geo = dynamic_cast<ExecutableNodeGeometry&>(ngo.nodeScene()->nodeGeometry());
    QSize size = geo.size(ngo.nodeId());

    ExecutionMode mode = execModel->executionMode();
    ExecutionState state = execModel->executionState();
    int progress = execModel->progress();
    bool isSelected = ngo.isSelected();

    // Antialiasing
    painter->setRenderHint(QPainter::Antialiasing);

    // Get context widget for theme detection
    ::QWidget* context = nullptr;
    if (!ngo.nodeScene()->views().isEmpty()) {
        QGraphicsView* view = ngo.nodeScene()->views().first();
        context = (QWidget*)view;
    }

    // Main card rectangle
    QRectF cardBounds(0, 0, size.width(), size.height());
    double const radius = 2.0; // Small rounded corners (rounded-sm)

    painter->save();
    
    // Step 1: Draw shadow - different based on state
    QColor shadowColor;
    qreal shadowOffset = 2.0;
    if (state == ExecutionState::Running) {
        shadowColor = isDarkTheme(context) ? QColor(0, 0, 0, 100) : QColor(0, 0, 0, 60);
    } else {
        shadowColor = isDarkTheme(context) ? QColor(0, 0, 0, 60) : QColor(0, 0, 0, 30);
    }
    
    QRectF shadowBounds(shadowOffset, shadowOffset, size.width(), size.height());
    painter->setPen(Qt::NoPen);
    painter->setBrush(shadowColor);
    painter->drawRoundedRect(shadowBounds, radius, radius);
    
    // Step 2: Draw main card background
    QColor bgColor = themedColor(QColor(255, 255, 255), QColor(50, 50, 50), context);
    
    // Get pen based on selection - border color #c0c7d4
    auto &nodeStyle = StyleCollection::nodeStyle();
    QPen pen(nodeStyle.SelectedBoundaryColor, 2.0);
    QPen normalPen(isDarkTheme(context) ? QColor(80, 80, 80) : QColor(192, 199, 212), 1.0);
    painter->setPen(isSelected ? pen : normalPen);
    
    painter->setBrush(bgColor);
    painter->drawRoundedRect(cardBounds, radius, radius);

    // Calculate rectangles for header/content/footer
    QRectF headerRect(0, 0, size.width(), CARD_HEADER_HEIGHT);
    QRectF footerRect(0, size.height() - CARD_FOOTER_HEIGHT, size.width(), CARD_FOOTER_HEIGHT);

    // Draw header
    drawCardHeader(painter, ngo, headerRect, mode, state, context);

    // Draw footer (status bar)
    drawCardFooter(painter, ngo, footerRect, state, progress, context);

    // Draw standard node elements (caption is drawn in header now, but need ports)
    // We still need the default painter to draw connection points and ports
    _defaultPainter.drawConnectionPoints(painter, ngo);
    _defaultPainter.drawFilledConnectionPoints(painter, ngo);

    // Draw port entry labels with background
    {
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 55));

        AbstractGraphModel &model = ngo.graphModel();
        AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();
        QFontMetrics fm(painter->font());

        for (PortType portType : {PortType::Out, PortType::In}) {
            unsigned int n = model.nodeData<unsigned int>(ngo.nodeId(),
                                  (portType == PortType::Out)
                                      ? NodeRole::OutPortCount
                                      : NodeRole::InPortCount);

            for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
                QString s;

                if (model.portData<bool>(ngo.nodeId(), portType, portIndex, PortRole::CaptionVisible)) {
                    s = model.portData<QString>(ngo.nodeId(), portType, portIndex, PortRole::Caption);
                } else {
                    auto portData = model.portData(ngo.nodeId(), portType, portIndex, PortRole::DataType);
                    s = portData.value<NodeDataType>().name;
                }

                if (!s.isEmpty()) {
                    QPointF baseline = geometry.portTextPosition(ngo.nodeId(), portType, portIndex);
                    QRectF bounds = fm.boundingRect(s);

                    double ascent = fm.ascent();
                    QPointF topLeft(baseline.x(), baseline.y() - ascent);
                    bounds.moveTopLeft(topLeft);

                    painter->drawRoundedRect(bounds.adjusted(-1, -1, 1, 1), 1.0, 1.0);
                }
            }
        }
        painter->restore();
    }

    _defaultPainter.drawEntryLabels(painter, ngo);
    _defaultPainter.drawResizeRect(painter, ngo);
}

void ExecutableNodePainter::drawCardHeader(QPainter *painter, NodeGraphicsObject &ngo,
                                             QRectF bounds, ExecutionMode mode,
                                             ExecutionState state, ::QWidget* context) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId nodeId = ngo.nodeId();

    // Determine header colors based on mode (matching design)
    QColor bgColor, textColor;

    if (mode == ExecutionMode::Automatic) {
        // Automatic mode: background #005fac (RGB 0, 95, 172), white text
        bgColor = themedColor(QColor(0, 95, 172), QColor(0, 95, 172), context);
        textColor = QColor(255, 255, 255);
    } else {
        // Manual mode: light background, amber (#994700) bottom border, dark text
        bgColor = themedColor(QColor(255, 255, 255), QColor(50, 50, 50), context);
        textColor = themedColor(QColor(26, 28, 28), QColor(220, 220, 220), context);
    }

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(bgColor);
    
    // Draw rounded header top corners - small radius
    QPainterPath path;
    path.moveTo(bounds.left(), bounds.bottom());
    path.lineTo(bounds.left(), bounds.top() + 2.0);
    path.quadTo(bounds.left(), bounds.top(), bounds.left() + 2.0, bounds.top());
    path.lineTo(bounds.right() - 2.0, bounds.top());
    path.quadTo(bounds.right(), bounds.top(), bounds.right(), bounds.top() + 2.0);
    path.lineTo(bounds.right(), bounds.bottom());
    path.lineTo(bounds.left(), bounds.bottom());
    painter->drawPath(path);

    // Add bottom border for manual mode
    if (mode == ExecutionMode::Manual) {
        painter->setPen(QPen(themedColor(QColor(153, 71, 0), QColor(153, 71, 0), context), 2));
        painter->drawLine(bounds.bottomLeft(), bounds.bottomRight());
    }
    painter->restore();

    // Draw caption (node name) - 11px bold, tracking tight
    if (model.nodeData(nodeId, NodeRole::CaptionVisible).toBool()) {
        QString const name = model.nodeData(nodeId, NodeRole::Caption).toString();

        painter->save();
        QFont f = painter->font();
        f.setBold(true);
        f.setPointSize(10); // text-[11px] in design
        painter->setFont(f);
        painter->setPen(textColor);

        // Position: left after icon (16px + 8px)
        painter->drawText(QRectF(28, 0, bounds.width() - 28 - 60, bounds.height()),
                         Qt::AlignVCenter, name);
        painter->restore();
    }

    // Draw mode icon on the left
    painter->save();
    QPixmap const &pixmap = (mode == ExecutionMode::Automatic) ? _pixmapAutomatic : _pixmapManual;
    double left = 4;
    double top = bounds.top() + (bounds.height() - pixmap.height()) / 2.0;
    QPoint topLeft = QPoint(static_cast<int>(left), static_cast<int>(top));

    // Recolor icon for automatic vs manual text contrast
    if (mode == ExecutionMode::Automatic) {
        // Already correctly colored
        painter->drawPixmap(topLeft, pixmap);
    } else {
        // Manual mode needs icon color to match text color
        // Draw with tint matching text color
        QImage img = pixmap.toImage();
        QColor tint = textColor;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                QColor pixel = img.pixelColor(x, y);
                if (pixel.alpha() > 0) {
                    pixel.setRgb(tint.red(), tint.green(), tint.blue(), pixel.alpha());
                    img.setPixelColor(x, y, pixel);
                }
            }
        }
        painter->drawPixmap(topLeft, QPixmap::fromImage(img));
    }
    painter->restore();

    // Draw buttons on the right
    drawCardHeaderButtons(painter, bounds, mode, state);
}

void ExecutableNodePainter::drawCardHeaderButtons(QPainter *painter, QRectF bounds,
                                                    ExecutionMode mode, ExecutionState state) const
{
    // Right to left: eye icon, then play/stop if manual - with better spacing
    double currentRight = bounds.right() - 12;

    // Draw eye icon always - with better spacing
    double eyeLeft = currentRight - _pixmapEye.width();
    double eyeTop = bounds.top() + (bounds.height() - _pixmapEye.height()) / 2.0;
    painter->drawPixmap(QPoint(static_cast<int>(eyeLeft), static_cast<int>(eyeTop)), _pixmapEye);
    currentRight = eyeLeft - 16;

    // Draw play/stop button - always visible, with better spacing
    QPixmap const &pixmap = (state == ExecutionState::Running) ? _pixmapStop : _pixmapPlay;
    double buttonLeft = currentRight - pixmap.width();
    double buttonTop = bounds.top() + (bounds.height() - pixmap.height()) / 2.0;
    
    if (state == ExecutionState::Completed) {
        painter->setOpacity(0.5);
    }
    painter->drawPixmap(QPoint(static_cast<int>(buttonLeft), static_cast<int>(buttonTop)), pixmap);
    if (state == ExecutionState::Completed) {
        painter->setOpacity(1.0);
    }
}

void ExecutableNodePainter::drawCardFooter(QPainter *painter, NodeGraphicsObject &/*ngo*/,
                                             QRectF bounds, ExecutionState state,
                                             int progress, ::QWidget* context) const
{
    painter->save();

    // Footer colors based on design
    QColor bgColor;
    QColor textColor;
    QColor borderColor;
    QPixmap const *stateIcon;

    // Select colors based on state (from design)
    switch (state) {
    case ExecutionState::Idle:
        bgColor = themedColor(QColor(242, 244, 246), QColor(55, 55, 55), context);
        textColor = themedColor(QColor(114, 118, 122), QColor(148, 163, 184), context);
        borderColor = themedColor(QColor(230, 232, 235), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateIdle;
        break;
    case ExecutionState::Pending:
        bgColor = themedColor(QColor(221, 236, 255), QColor(30, 58, 138), context);
        textColor = themedColor(QColor(0, 95, 172), QColor(96, 165, 250), context);
        borderColor = themedColor(QColor(196, 222, 255), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStatePending;
        break;
    case ExecutionState::Running:
        bgColor = themedColor(QColor(242, 248, 255), QColor(30, 58, 138), context);
        textColor = themedColor(QColor(0, 95, 172), QColor(96, 165, 250), context);
        borderColor = themedColor(QColor(221, 236, 255), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateRunning;
        break;
    case ExecutionState::Completed:
        bgColor = themedColor(QColor(238, 249, 235), QColor(20, 83, 45), context);
        textColor = themedColor(QColor(48, 122, 60), QColor(74, 222, 128), context);
        borderColor = themedColor(QColor(226, 243, 221), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateCompleted;
        break;
    case ExecutionState::Stopped:
        bgColor = themedColor(QColor(255, 250, 235), QColor(120, 53, 15), context);
        textColor = themedColor(QColor(153, 113, 0), QColor(251, 191, 36), context);
        borderColor = themedColor(QColor(255, 243, 215), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateStopped;
        break;
    case ExecutionState::Warning:
        bgColor = themedColor(QColor(255, 250, 235), QColor(120, 53, 15), context);
        textColor = themedColor(QColor(153, 113, 0), QColor(251, 191, 36), context);
        borderColor = themedColor(QColor(255, 243, 215), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateWarning;
        break;
    case ExecutionState::Error:
        bgColor = themedColor(QColor(255, 238, 238), QColor(127, 29, 29), context);
        textColor = themedColor(QColor(180, 69, 69), QColor(248, 113, 113), context);
        borderColor = themedColor(QColor(255, 214, 214), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateError;
        break;
    case ExecutionState::Disabled:
        bgColor = themedColor(QColor(242, 244, 246), QColor(55, 55, 55), context);
        textColor = themedColor(QColor(165, 171, 177), QColor(148, 163, 184), context);
        borderColor = themedColor(QColor(230, 232, 235), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateDisabled;
        break;
    default:
        bgColor = themedColor(QColor(242, 244, 246), QColor(55, 55, 55), context);
        textColor = themedColor(QColor(114, 118, 122), QColor(148, 163, 184), context);
        borderColor = themedColor(QColor(230, 232, 235), QColor(70, 70, 70), context);
        stateIcon = &_pixmapStateIdle;
        break;
    }

    // Draw footer background - with small rounded bottom corners
    painter->setPen(Qt::NoPen);
    painter->setBrush(bgColor);
    QPainterPath path;
    path.moveTo(bounds.left(), bounds.top());
    path.lineTo(bounds.left(), bounds.bottom() - 2.0);
    path.quadTo(bounds.left(), bounds.bottom(), bounds.left() + 2.0, bounds.bottom());
    path.lineTo(bounds.right() - 2.0, bounds.bottom());
    path.quadTo(bounds.right(), bounds.bottom(), bounds.right(), bounds.bottom() - 2.0);
    path.lineTo(bounds.right(), bounds.top());
    path.lineTo(bounds.left(), bounds.top());
    painter->drawPath(path);
    
    // Draw top border
    painter->setPen(QPen(borderColor, 1.0));
    painter->drawLine(bounds.topLeft(), bounds.topRight());
    painter->restore();

    painter->save();

    // Draw progress bar when running (full-width at bottom layer)
    double filledWidth = 0;
    if (state == ExecutionState::Running && progress > 0) {
        // Draw full-width progress bar (solid color, not transparent)
        QColor progressColor = themedColor(QColor(0, 95, 172), QColor(0, 95, 172), context);

        filledWidth = bounds.width() * progress / 100.0;
        QRectF filledRect = bounds;
        filledRect.setWidth(filledWidth);

        // Draw the filled progress area with rounded bottom corners (match footer style)
        QPainterPath progressPath;
        progressPath.moveTo(filledRect.left(), filledRect.top());
        progressPath.lineTo(filledRect.left(), filledRect.bottom() - 2.0);

        // Only round bottom corners if filled area covers entire footer width
        if (filledWidth >= bounds.width()) {
            progressPath.quadTo(filledRect.left(), filledRect.bottom(), filledRect.left() + 2.0, filledRect.bottom());
            progressPath.lineTo(filledRect.right() - 2.0, filledRect.bottom());
            progressPath.quadTo(filledRect.right(), filledRect.bottom(), filledRect.right(), filledRect.bottom() - 2.0);
        } else {
            progressPath.lineTo(filledRect.left(), filledRect.bottom());
            progressPath.lineTo(filledRect.right(), filledRect.bottom());
            progressPath.lineTo(filledRect.right(), filledRect.top());
        }
        // Path is already closed after moveTo + lines, no need for extra line back to start

        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(progressColor);
        painter->drawPath(progressPath);
        painter->restore();
    }

    // Get state name - uppercase
    QString stateName;
    switch (state) {
    case ExecutionState::Idle: stateName = "IDLE"; break;
    case ExecutionState::Pending: stateName = "READY"; break;
    case ExecutionState::Running: stateName = "RUNNING"; break;
    case ExecutionState::Completed: stateName = "COMPLETED"; break;
    case ExecutionState::Stopped: stateName = "STOPPED"; break;
    case ExecutionState::Warning: stateName = "WARNING"; break;
    case ExecutionState::Error: stateName = "ERROR"; break;
    case ExecutionState::Disabled: stateName = "DISABLED"; break;
    }

    // Draw state icon on the left (12px from left, vertically centered)
    double iconLeft = 12; // px-3 left
    double iconTop = bounds.top() + (bounds.height() - stateIcon->height()) / 2.0;
    painter->drawPixmap(static_cast<int>(iconLeft), static_cast<int>(iconTop), *stateIcon);

    // Prepare font once for all cases
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(8);
    font.setCapitalization(QFont::AllUppercase);

    double textLeft = iconLeft + stateIcon->width() + 6;

    if (state == ExecutionState::Running && progress > 0) {
        // Smart text drawing: white on progress, gray on non-progress
        QString percentText = QString("%1%").arg(progress);
        QFontMetrics fm(font);
        double textWidth = fm.horizontalAdvance(percentText) + 10;

        painter->save();

        // 1. Draw white text on progress area (clipped)
        QRectF progressRect = bounds;
        progressRect.setWidth(filledWidth);
        painter->setClipRect(progressRect);

        painter->setPen(themedColor(QColor(255, 255, 255), QColor(255, 255, 255), context));
        painter->setFont(font);
        painter->drawText(QRectF(textLeft, bounds.top(), 80, bounds.height()),
                         Qt::AlignVCenter, stateName);
        painter->drawText(QRectF(bounds.width() - textWidth, bounds.top(), textWidth - 5, bounds.height()),
                         Qt::AlignVCenter | Qt::AlignRight, percentText);

        // 2. Draw gray text on non-progress area (clipped)
        QRectF nonProgressRect = bounds.adjusted(filledWidth, 0, 0, 0);
        painter->setClipRect(nonProgressRect);

        painter->setPen(textColor);
        painter->drawText(QRectF(textLeft, bounds.top(), 80, bounds.height()),
                         Qt::AlignVCenter, stateName);
        painter->drawText(QRectF(bounds.width() - textWidth, bounds.top(), textWidth - 5, bounds.height()),
                         Qt::AlignVCenter | Qt::AlignRight, percentText);

        painter->restore();
    } else {
        // Normal text drawing when not running
        painter->save();
        painter->setPen(textColor);
        painter->setFont(font);
        painter->drawText(QRectF(textLeft, bounds.top(), 80, bounds.height()),
                         Qt::AlignVCenter, stateName);
        painter->restore();
    }
}

QColor ExecutableNodePainter::stateColor(ExecutionState /*state*/) const
{
    // This method is currently unused but kept for potential future use
    return QColor(136, 136, 136);
}

QColor ExecutableNodePainter::gradientStartColor(ExecutionMode mode, ExecutionState state, QWidget* context) const
{
    Q_UNUSED(mode);

    // Get context for theme detection
    QWidget* themeContext = context;

    switch (state) {
    case ExecutionState::Idle:
        return themedColor(::COLOR_IDLE_START_L, ::COLOR_IDLE_START_D, themeContext);
    case ExecutionState::Pending:
        return themedColor(::COLOR_PENDING_L, ::COLOR_PENDING_D, themeContext);
    case ExecutionState::Running:
        return themedColor(::COLOR_RUNNING_START_L, ::COLOR_RUNNING_START_D, themeContext);
    case ExecutionState::Completed:
        return themedColor(::COLOR_COMPLETED_L, ::COLOR_COMPLETED_D, themeContext);
    case ExecutionState::Stopped:
        return themedColor(::COLOR_STOPPED_START_L, ::COLOR_STOPPED_START_D, themeContext);
    case ExecutionState::Warning:
        return themedColor(::COLOR_WARNING_START_L, ::COLOR_WARNING_START_D, themeContext);
    case ExecutionState::Error:
        return themedColor(::COLOR_ERROR_START_L, ::COLOR_ERROR_START_D, themeContext);
    case ExecutionState::Disabled:
        return themedColor(::COLOR_DISABLED_L, ::COLOR_DISABLED_D, themeContext);
    }
    return themedColor(::COLOR_IDLE_START_L, ::COLOR_IDLE_START_D, themeContext);
}

QColor ExecutableNodePainter::gradientEndColor(ExecutionMode mode, ExecutionState state, QWidget* context) const
{
    Q_UNUSED(mode);

    // Get context for theme detection
    QWidget* themeContext = context;

    switch (state) {
    case ExecutionState::Idle:
        return themedColor(::COLOR_IDLE_END_L, ::COLOR_IDLE_END_D, themeContext);
    case ExecutionState::Pending:
        return themedColor(::COLOR_PENDING_L, ::COLOR_PENDING_D, themeContext);
    case ExecutionState::Running:
        return themedColor(::COLOR_RUNNING_END_L, ::COLOR_RUNNING_END_D, themeContext);
    case ExecutionState::Completed:
        return themedColor(::COLOR_COMPLETED_L, ::COLOR_COMPLETED_D, themeContext);
    case ExecutionState::Stopped:
        return themedColor(::COLOR_STOPPED_END_L, ::COLOR_STOPPED_END_D, themeContext);
    case ExecutionState::Warning:
        return themedColor(::COLOR_WARNING_END_L, ::COLOR_WARNING_END_D, themeContext);
    case ExecutionState::Error:
        return themedColor(::COLOR_ERROR_END_L, ::COLOR_ERROR_END_D, themeContext);
    case ExecutionState::Disabled:
        return themedColor(::COLOR_DISABLED_L, ::COLOR_DISABLED_D, themeContext);
    }
    return themedColor(::COLOR_IDLE_END_L, ::COLOR_IDLE_END_D, themeContext);
}

} // namespace QtNodes

// ============================================================================
// Theme Detection Helper Functions
// ============================================================================

bool QtNodes::ExecutableNodePainter::isDarkTheme(QWidget* widget)
{
    if (!widget) return false;

    // Check if parent window uses dark theme by examining background color
    // Dark theme: #353535 or #2B2B2B
    // Light theme: #F5F5F5 or white colors

    QVariant bgColor = widget->property("theme-background");
    if (bgColor.isValid()) {
        QColor color = bgColor.value<QColor>();
        // Check for dark theme colors
        if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
            return true;  // Dark theme
        }
    }

    return false;  // Default to light theme
}

QColor QtNodes::ExecutableNodePainter::themedColor(const QColor& lightColor, const QColor& darkColor, QWidget* context) const
{
    return isDarkTheme(context) ? darkColor : lightColor;
}
