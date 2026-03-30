#include "QtNodes/internal/NodeDetailAnimationController.hpp"
#include "QtNodes/internal/NodeGraphicsObject.hpp"
#include "QtNodes/internal/NodeDetailWindow.hpp"
#include "QtNodes/internal/NodeDetailOverlay.hpp"
#include <QTransform>
#include <QGraphicsScale>
#include <QVector3D>
#include <QRectF>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QGuiApplication>
#include <QScreen>

namespace QtNodes {

NodeDetailAnimationController::NodeDetailAnimationController(QObject* parent)
    : QObject(parent)
    , _node(nullptr)
    , _window(nullptr)
    , _overlay(nullptr)
    , _nodeScaleTransform(nullptr)
    , _flipCollapseAnimation(nullptr)
    , _flipExpandAnimation(nullptr)
    , _windowFadeInAnimation(nullptr)
    , _windowFadeOutAnimation(nullptr)
    , _overlayFadeInAnimation(nullptr)
    , _overlayFadeOutAnimation(nullptr)
    , _detailViewZoomInAnimation(nullptr)
    , _detailViewZoomOutAnimation(nullptr)
    , _openAnimationGroup(new QSequentialAnimationGroup(this))
    , _closeAnimationGroup(new QSequentialAnimationGroup(this))
    , _isAnimating(false)
    , _isOpening(false)
{
}

void NodeDetailAnimationController::startOpenAnimation(NodeGraphicsObject* node,
                                                      NodeDetailWindow* window,
                                                      NodeDetailOverlay* overlay)
{
    if (_isAnimating) {
        return;
    }

    _node = node;
    _window = window;
    _overlay = overlay;
    _isOpening = true;
    _isAnimating = true;

    setupOpenAnimations(node, window, overlay);

    _openAnimationGroup->start();
}

void NodeDetailAnimationController::startCloseAnimation(NodeGraphicsObject* node,
                                                       NodeDetailWindow* window,
                                                       NodeDetailOverlay* overlay)
{
    if (_isAnimating) {
        return;
    }

    _node = node;
    _window = window;
    _overlay = overlay;
    _isOpening = false;
    _isAnimating = true;

    setupCloseAnimations(node, window, overlay);

    _closeAnimationGroup->start();
}

void NodeDetailAnimationController::setupOpenAnimations(NodeGraphicsObject* node,
                                                         NodeDetailWindow* window,
                                                         NodeDetailOverlay* overlay)
{
    // Clear previous animations
    _openAnimationGroup->clear();

    // Calculate final window geometry (screen center)
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    QPoint screenCenter = screenRect.center();
    QRect finalWindowRect(screenCenter.x() - window->width() / 2,
                         screenCenter.y() - window->height() / 2,
                         window->width(),
                         window->height());

    // Save final window geometry to cache for close animation
    _cachedFinalWindowRect = finalWindowRect;

    // Calculate initial window geometry (node size, at node position)
    QRectF nodeBounds = node->boundingRect();
    QPointF nodeScenePos = node->scenePos();
    QGraphicsView* view = node->scene()->views().first();
    QPoint nodeScreenPosInt = view->mapToGlobal(view->mapFromScene(nodeScenePos));

    QRect initialWindowRect(nodeScreenPosInt.x() + nodeBounds.x(),
                          nodeScreenPosInt.y() + nodeBounds.y(),
                          nodeBounds.width(),
                          nodeBounds.height());

    // Save initial window geometry to cache for close animation
    _cachedInitialWindowRect = initialWindowRect;

    // Set initial window geometry and opacity
    window->setGeometry(initialWindowRect);
    overlay->opacityEffect()->setOpacity(0.0);  // Set initial overlay opacity BEFORE animation
    window->setWindowOpacity(0.0);
    // DO NOT show overlay here - overlay will be shown when zoomInGroup starts

    // Phase 1: Flip collapse (xScale: 1 → -1) - flip to back side
    _nodeScaleTransform = new QGraphicsScale(this);
    _nodeScaleTransform->setXScale(1.0);
    _nodeScaleTransform->setYScale(1.0);

    // Set origin to center of node for proper flip effect
    QVector3D flipOrigin(nodeBounds.center().x(), nodeBounds.center().y(), 0.0);
    _nodeScaleTransform->setOrigin(flipOrigin);

    node->setTransformations(QList<QGraphicsTransform*>() << _nodeScaleTransform);

    _flipCollapseAnimation = new QPropertyAnimation(_nodeScaleTransform, "xScale", this);
    _flipCollapseAnimation->setDuration(FLIP_ANIMATION_DURATION);
    _flipCollapseAnimation->setStartValue(1.0);
    _flipCollapseAnimation->setEndValue(-1.0);

    // Hide node when flip passes through 0 (the middle point)
    connect(_flipCollapseAnimation, &QPropertyAnimation::valueChanged,
            [this, node](const QVariant& value) {
                qreal scaleX = value.toReal();
                if (scaleX < 0.0 && node->isVisible()) {
                    node->hide();
                }
            });

    // Phase 2: Window fade in (when flip reaches middle)
    _windowFadeInAnimation = new QPropertyAnimation(window, "windowOpacity", this);
    _windowFadeInAnimation->setDuration(WINDOW_FADE_DURATION);
    _windowFadeInAnimation->setStartValue(0.0);
    _windowFadeInAnimation->setEndValue(1.0);

    // Connect: show window when fade in starts
    connect(_windowFadeInAnimation, &QPropertyAnimation::stateChanged,
            [this, window, initialWindowRect](QAbstractAnimation::State newState, QAbstractAnimation::State) {
                if (newState == QAbstractAnimation::Running) {
                    // Force set window size to node size using setFixedSize
                    window->setFixedSize(initialWindowRect.width(), initialWindowRect.height());
                    window->move(initialWindowRect.topLeft());
                    window->show();
                }
            });

    // Phase 3: Detail view zoom in + overlay fade in (parallel)
    QParallelAnimationGroup* zoomInGroup = new QParallelAnimationGroup(this);

    // Animate window geometry: position and size simultaneously
    _detailViewZoomInAnimation = new QPropertyAnimation(window, "geometry", this);
    _detailViewZoomInAnimation->setDuration(ZOOM_ANIMATION_DURATION);
    _detailViewZoomInAnimation->setStartValue(initialWindowRect);
    _detailViewZoomInAnimation->setEndValue(finalWindowRect);
    _detailViewZoomInAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // Animate overlay opacity: fade in while zooming
    // Both animations have the SAME duration to ensure perfect synchronization
    // Use QGraphicsOpacityEffect for child widget opacity animation (windowOpacity only works for top-level windows)
    _overlayFadeInAnimation = new QPropertyAnimation(overlay->opacityEffect(), "opacity", this);
    _overlayFadeInAnimation->setDuration(_detailViewZoomInAnimation->duration());
    _overlayFadeInAnimation->setStartValue(0.0);
    _overlayFadeInAnimation->setEndValue(1.0);

    zoomInGroup->addAnimation(_detailViewZoomInAnimation);
    zoomInGroup->addAnimation(_overlayFadeInAnimation);

    // Show overlay and node (in flipped state) when zoom in starts
    connect(zoomInGroup, &QParallelAnimationGroup::stateChanged,
            [this, node, window, overlay, initialWindowRect](QAbstractAnimation::State newState, QAbstractAnimation::State) {
                if (newState == QAbstractAnimation::Running) {
                    // Remove fixed size constraint and ensure correct starting geometry
                    window->setMinimumSize(0, 0);
                    window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    window->setGeometry(initialWindowRect);  // Force set to initial value

                    // Show overlay HERE, before animation starts, with initial opacity already set to 0
                    // This ensures the fade-in animation starts from the correct value
                    overlay->opacityEffect()->setOpacity(0.0);
                    overlay->showOverlay();

                    node->show();
                }
            });

    // Connect: cleanup on completion
    connect(_openAnimationGroup, &QSequentialAnimationGroup::finished,
            this, &NodeDetailAnimationController::onOpenAnimationCompleted);

    // Build sequence: flip collapse → window fade in → (zoom in + overlay fade in)
    _openAnimationGroup->addAnimation(_flipCollapseAnimation);
    _openAnimationGroup->addAnimation(_windowFadeInAnimation);
    _openAnimationGroup->addAnimation(zoomInGroup);
}

void NodeDetailAnimationController::setupCloseAnimations(NodeGraphicsObject* node,
                                                          NodeDetailWindow* window,
                                                          NodeDetailOverlay* overlay)
{
    // Clear previous animations
    _closeAnimationGroup->clear();

    // Use cached geometries from open animation instead of recalculating
    // This ensures close animation returns to the exact same position as open started
    QRect finalWindowRect = _cachedFinalWindowRect;
    QRect targetWindowRect = _cachedInitialWindowRect;

    // Phase 1: Detail view zoom out + overlay fade out (parallel)
    QParallelAnimationGroup* zoomOutGroup = new QParallelAnimationGroup(this);

    _detailViewZoomOutAnimation = new QPropertyAnimation(window, "geometry", this);
    _detailViewZoomOutAnimation->setDuration(ZOOM_ANIMATION_DURATION);
    _detailViewZoomOutAnimation->setStartValue(finalWindowRect);
    _detailViewZoomOutAnimation->setEndValue(targetWindowRect);
    _detailViewZoomOutAnimation->setEasingCurve(QEasingCurve::InCubic);

    // Ensure overlay starts with full opacity before fade out animation
    overlay->opacityEffect()->setOpacity(1.0);

    // Animate overlay opacity: fade out while zooming
    // Both animations have the SAME duration to ensure perfect synchronization
    // Use QGraphicsOpacityEffect for child widget opacity animation (windowOpacity only works for top-level windows)
    _overlayFadeOutAnimation = new QPropertyAnimation(overlay->opacityEffect(), "opacity", this);
    _overlayFadeOutAnimation->setDuration(_detailViewZoomOutAnimation->duration());
    _overlayFadeOutAnimation->setStartValue(1.0);
    _overlayFadeOutAnimation->setEndValue(0.0);

    zoomOutGroup->addAnimation(_detailViewZoomOutAnimation);
    zoomOutGroup->addAnimation(_overlayFadeOutAnimation);

    // Phase 2: Hide window after zoom out (overlay already faded out)
    connect(zoomOutGroup, &QParallelAnimationGroup::finished,
            [this]() {
                if (_window) _window->hide();
                if (_overlay) _overlay->hideOverlay();
            });

    // Phase 3: Flip expand (xScale: -1 → 1) - restore node visibility
    _flipExpandAnimation = new QPropertyAnimation(_nodeScaleTransform, "xScale", this);
    _flipExpandAnimation->setDuration(FLIP_ANIMATION_DURATION);
    _flipExpandAnimation->setStartValue(-1.0);
    _flipExpandAnimation->setEndValue(1.0);

    // Show node when flip expand starts
    connect(_flipExpandAnimation, &QPropertyAnimation::stateChanged,
            [this, node](QAbstractAnimation::State newState, QAbstractAnimation::State) {
                if (newState == QAbstractAnimation::Running) {
                    node->show();
                }
            });

    // Connect: cleanup and signal
    connect(_closeAnimationGroup, &QSequentialAnimationGroup::finished,
            this, &NodeDetailAnimationController::onCloseAnimationCompleted);

    // Build sequence: zoom out → flip expand
    _closeAnimationGroup->addAnimation(zoomOutGroup);
    _closeAnimationGroup->addAnimation(_flipExpandAnimation);
}

void NodeDetailAnimationController::onOpenAnimationCompleted()
{
    _isAnimating = false;
    Q_EMIT openAnimationCompleted();
}

void NodeDetailAnimationController::onCloseAnimationCompleted()
{
    // Remove the scale transform from node
    if (_node && _nodeScaleTransform) {
        _node->setTransformations(QList<QGraphicsTransform*>());
        _nodeScaleTransform->deleteLater();
        _nodeScaleTransform = nullptr;
    }

    if (_window) {
        _window->deleteLater();
        _window = nullptr;
    }

    // Select the node after closing
    if (_node) {
        _node->setSelected(true);
    }

    _isAnimating = false;
    Q_EMIT closeAnimationCompleted();
}

} // namespace QtNodes
