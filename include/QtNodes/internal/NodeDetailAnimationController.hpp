#pragma once

#include "Export.hpp"
#include <QtCore/QObject>
#include <QtCore/QPropertyAnimation>
#include <QtCore/QSequentialAnimationGroup>
#include <QtCore/QParallelAnimationGroup>
#include <QtCore/QRect>

// Forward declarations (Qt classes)
class QGraphicsScale;

namespace QtNodes {

// Forward declarations (QtNodes classes)
class NodeGraphicsObject;
class NodeDetailWindow;
class NodeDetailOverlay;

/// Controller for orchestrating flip and zoom animations for detail view
/// Manages the complete transition sequence: flip → hide/show → zoom
class NODE_EDITOR_PUBLIC NodeDetailAnimationController : public QObject
{
    Q_OBJECT

public:
    explicit NodeDetailAnimationController(QObject* parent = nullptr);
    ~NodeDetailAnimationController() override = default;

    /// Start the open animation sequence
    /// Flips the node horizontally (scaleX: 1 → 0 → 1), shows overlay and detail window
    void startOpenAnimation(NodeGraphicsObject* node, NodeDetailWindow* window,
                          NodeDetailOverlay* overlay);

    /// Start the close animation sequence
    /// Reverses the open animation: hide window, show node
    void startCloseAnimation(NodeGraphicsObject* node, NodeDetailWindow* window,
                           NodeDetailOverlay* overlay);

    /// Check if animation is currently running
    bool isAnimating() const { return _isAnimating; }

Q_SIGNALS:
    /// Emitted when open animation completes
    void openAnimationCompleted();

    /// Emitted when close animation completes
    void closeAnimationCompleted();

private:
    void setupOpenAnimations(NodeGraphicsObject* node, NodeDetailWindow* window,
                           NodeDetailOverlay* overlay);
    void setupCloseAnimations(NodeGraphicsObject* node, NodeDetailWindow* window,
                            NodeDetailOverlay* overlay);

    /// Slot called when open animation completes
    void onOpenAnimationCompleted();

    /// Slot called when close animation completes
    void onCloseAnimationCompleted();

    NodeGraphicsObject* _node;
    NodeDetailWindow* _window;
    NodeDetailOverlay* _overlay;
    QGraphicsScale* _nodeScaleTransform;

    QPropertyAnimation* _flipCollapseAnimation;
    QPropertyAnimation* _flipExpandAnimation;
    QPropertyAnimation* _windowFadeInAnimation;
    QPropertyAnimation* _windowFadeOutAnimation;
    QPropertyAnimation* _overlayFadeInAnimation;
    QPropertyAnimation* _overlayFadeOutAnimation;
    QPropertyAnimation* _detailViewZoomInAnimation;
    QPropertyAnimation* _detailViewZoomOutAnimation;

    QSequentialAnimationGroup* _openAnimationGroup;
    QSequentialAnimationGroup* _closeAnimationGroup;

    bool _isAnimating;
    bool _isOpening;

    // Cached window geometries (saved during open, reused during close)
    QRect _cachedInitialWindowRect;  // Window geometry at node position/size
    QRect _cachedFinalWindowRect;     // Window geometry at screen center

    // Animation timing constants (in milliseconds)
    static constexpr int FLIP_ANIMATION_DURATION = 400;      // Node flip (both open and close)
    static constexpr int ZOOM_ANIMATION_DURATION = 800;     // Zoom in/out and overlay fade in/out
    static constexpr int WINDOW_FADE_DURATION = 50;         // Initial window fade in (separate step)
};

} // namespace QtNodes
