#include "QtNodes/internal/NodeDetailOverlay.hpp"
#include <QPainter>
#include <QPaintEvent>

namespace QtNodes {

NodeDetailOverlay::NodeDetailOverlay(QWidget* parent)
    : QWidget(parent)
    , _overlayColor(15, 23, 42, 102)  // Deep blue semi-transparent (rgba(15, 23, 42, 0.4))
    , _opacityEffect(new QGraphicsOpacityEffect(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setGraphicsEffect(_opacityEffect);
    _opacityEffect->setOpacity(0.0);  // Start invisible
}

void NodeDetailOverlay::showOverlay()
{
    show();
    raise();
}

void NodeDetailOverlay::hideOverlay()
{
    hide();
}

void NodeDetailOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);

    // Create radial gradient to simulate backdrop blur effect
    // Center is more transparent (blurrier), edges are more solid
    QRadialGradient gradient(rect().center(), qMax(rect().width(), rect().height()) * 0.7);

    // Center: rgba(15, 23, 42, 0.25) - very transparent (blurred)
    // Edge: rgba(15, 23, 42, 0.5) - more solid
    gradient.setColorAt(0.0, QColor(15, 23, 42, 64));
    gradient.setColorAt(1.0, QColor(15, 23, 42, 128));

    painter.setBrush(QBrush(gradient));
    painter.setPen(Qt::NoPen);
    painter.drawRect(rect());
}

} // namespace QtNodes
