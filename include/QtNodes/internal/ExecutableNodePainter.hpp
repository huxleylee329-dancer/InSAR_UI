#pragma once

#include "AbstractNodePainter.hpp"
#include "ExecutableNodeGeometry.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "DefaultNodePainter.hpp"

#include <QPixmap>

class QWidget;

namespace QtNodes {

class NodeGraphicsObject;

class NODE_EDITOR_PUBLIC ExecutableNodePainter : public AbstractNodePainter
{
public:
    ExecutableNodePainter();
    ~ExecutableNodePainter() override = default;

    void paint(QPainter *painter, NodeGraphicsObject &ngo) const override;

private:
    void drawMainRect(QPainter *painter, NodeGraphicsObject &ngo,
                      ExecutableNodeGeometry &geo,
                      ExecutionMode mode, ExecutionState state,
                      ::QWidget* context) const;
    void drawLeftEar(QPainter *painter, NodeGraphicsObject &ngo,
                     ExecutableNodeGeometry &geo,
                     ExecutionMode mode, ExecutionState state,
                     ::QWidget* context) const;
    void drawRightEar(QPainter *painter, NodeGraphicsObject &ngo,
                      ExecutableNodeGeometry &geo,
                      ExecutionState state,
                      ::QWidget* context) const;
    void drawProgressBar(QPainter *painter, NodeGraphicsObject &ngo,
                         ExecutableNodeGeometry &geo,
                         int progress,
                         ::QWidget* context) const;
    void drawStartButton(QPainter *painter, QRectF rect, ExecutionState state) const;

    // Card layout drawing
    void drawCardLayout(QPainter *painter, NodeGraphicsObject &ngo,
                         ExecutableNodeDelegateModel *execModel) const;
    void drawCardHeader(QPainter *painter, NodeGraphicsObject &ngo,
                        QRectF bounds, ExecutionMode mode,
                        ExecutionState state, ::QWidget* context) const;
    void drawCardFooter(QPainter *painter, NodeGraphicsObject &ngo,
                        QRectF bounds, ExecutionState state,
                        int progress, ::QWidget* context) const;
    void drawCardProgressBar(QPainter *painter, QRectF bounds,
                             int progress, ExecutionState state,
                             ::QWidget* context) const;
    void drawCardHeaderButtons(QPainter *painter, QRectF bounds,
                               ExecutionMode mode, ExecutionState state) const;

    /// Get gradient start color based on mode and state (theme-aware)
    QColor gradientStartColor(ExecutionMode mode, ExecutionState state, ::QWidget* context) const;
    /// Get gradient end color based on mode and state (theme-aware)
    QColor gradientEndColor(ExecutionMode mode, ExecutionState state, ::QWidget* context) const;

    QColor stateColor(ExecutionState state) const;

    /// Check if dark theme is active (detects from parent widget)
    static bool isDarkTheme(::QWidget* widget);
    /// Get theme-aware color (light or dark)
    QColor themedColor(const QColor& lightColor, const QColor& darkColor, ::QWidget* context) const;

private:
    QPixmap loadAndColorizeIcon(const QString &resourcePath, const QColor &color, const QSize &size);

private:
    QPixmap _pixmapAutomatic;
    QPixmap _pixmapManual;
    QPixmap _pixmapPlay;
    QPixmap _pixmapStop;
    QPixmap _pixmapEye;

    // State icons for card footer
    QPixmap _pixmapStateIdle;
    QPixmap _pixmapStatePending;
    QPixmap _pixmapStateRunning;
    QPixmap _pixmapStateCompleted;
    QPixmap _pixmapStateStopped;
    QPixmap _pixmapStateWarning;
    QPixmap _pixmapStateError;
    QPixmap _pixmapStateDisabled;

    // Reusable default painter instance for performance
    mutable DefaultNodePainter _defaultPainter;
};

} // namespace QtNodes
