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

// Initialize color scheme (DYNAMIC THEME ADAPTATION)
namespace {
    // Icon colors (PROFESSIONAL VERSION - softer, more cohesive)
    constexpr QColor COLOR_AUTOMATIC{59, 130, 246};  // #3B82F6 (system blue)
    constexpr QColor COLOR_MANUAL{245, 158, 11};     // #F59E0B (system amber)
    constexpr QColor COLOR_PLAY{16, 185, 129};        // #10B981 (professional green)
    constexpr QColor COLOR_STOP{239, 68, 68};        // #EF4444 (professional red)
    constexpr QColor COLOR_EYE{148, 163, 184};       // #94A3B8 (subtle gray)

    // State background colors - LIGHT THEME (Low saturation, cohesive with main app)
    constexpr QColor COLOR_IDLE_START_L{241, 245, 249};     // #F1F5F9
    constexpr QColor COLOR_IDLE_END_L{226, 232, 240};       // #E2E8F0
    constexpr QColor COLOR_PENDING_L{167, 243, 208};        // #A7F3D0
    constexpr QColor COLOR_RUNNING_START_L{59, 130, 246};    // #3B82F6
    constexpr QColor COLOR_RUNNING_END_L{96, 165, 250};      // #60A5FA
    constexpr QColor COLOR_COMPLETED_L{16, 185, 129};        // #10B981
    constexpr QColor COLOR_STOPPED_START_L{245, 158, 11};     // #F59E0B
    constexpr QColor COLOR_STOPPED_END_L{217, 119, 6};       // #D97706
    constexpr QColor COLOR_WARNING_START_L{251, 191, 36};       // #FBBF24
    constexpr QColor COLOR_WARNING_END_L{245, 158, 11};        // #F59E0B
    constexpr QColor COLOR_ERROR_START_L{239, 68, 68};         // #EF4444
    constexpr QColor COLOR_ERROR_END_L{220, 38, 38};          // #DC2626
    constexpr QColor COLOR_DISABLED_L{148, 163, 184};        // #94A3B8

    // State background colors - DARK THEME (Matches main app dark theme)
    constexpr QColor COLOR_IDLE_START_D{64, 64, 64};         // #404040
    constexpr QColor COLOR_IDLE_END_D{64, 64, 64};          // #404040
    constexpr QColor COLOR_PENDING_D{64, 64, 64};          // #404040
    constexpr QColor COLOR_RUNNING_START_D{43, 64, 75};       // #2B404B
    constexpr QColor COLOR_RUNNING_END_D{74, 116, 141};       // #4A748D
    constexpr QColor COLOR_COMPLETED_D{74, 169, 207};        // #4AA9CF
    constexpr QColor COLOR_STOPPED_START_D{251, 191, 36};   // #FBBF24
    constexpr QColor COLOR_STOPPED_END_D{217, 119, 6};       // #D97706
    constexpr QColor COLOR_WARNING_START_D{251, 191, 36};    // #FBBF24
    constexpr QColor COLOR_WARNING_END_D{217, 119, 6};       // #D97706
    constexpr QColor COLOR_ERROR_START_D{239, 68, 68};         // #EF4444
    constexpr QColor COLOR_ERROR_END_D{220, 38, 38};          // #DC2626
    constexpr QColor COLOR_DISABLED_D{80, 80, 80};           // #505050
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
    _pixmapAutomatic = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/refresh-cw.svg"), ::COLOR_AUTOMATIC, QSize(20, 20));
    _pixmapManual = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/refresh-cw-off.svg"), ::COLOR_MANUAL, QSize(20, 20));
    _pixmapPlay = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/play.svg"), ::COLOR_PLAY, QSize(16, 16));
    _pixmapStop = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/stop.svg"), ::COLOR_STOP, QSize(16, 16));
    _pixmapEye = loadAndColorizeIcon(QStringLiteral(":/SatExplorer/eye.svg"), ::COLOR_EYE, QSize(20, 20));
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
    painter->drawPixmap(topLeft, pixmap);
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
