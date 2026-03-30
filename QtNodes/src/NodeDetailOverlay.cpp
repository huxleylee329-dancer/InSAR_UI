#include "QtNodes/internal/NodeDetailOverlay.hpp"
#include <QPainter>
#include <QPaintEvent>

namespace QtNodes {

NodeDetailOverlay::NodeDetailOverlay(QWidget* parent)
    : QWidget(parent)
    , _overlayColor(0, 0, 0, 128)  // 50% opacity in color
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

    // Fill the entire widget with semi-transparent black
    painter.fillRect(rect(), _overlayColor);
}

} // namespace QtNodes
