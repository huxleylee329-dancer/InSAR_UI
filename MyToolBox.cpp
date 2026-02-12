#include<MyToolBox.h>
#include<MyPage.h>
#include<qevent.h>
#include<QScrollBar>

MyToolBox::MyToolBox(QWidget* parent) :
	QWidget(parent)
{
	initVar();
	createScrollArea();
	initLayout();
}

MyToolBox::~MyToolBox()
{

}

int MyToolBox::count() const
{
    return PageBar.count();
}

MyPage* MyToolBox::getItem(int index)
{
    if (index >= 0 && index < PageBar.count())
    {
        return PageBar[index];
    }
    else
    {
        return NULL;
    }
}

void MyToolBox::addItem(MyPage* page)
{
    if (page == NULL) return;

    PageBar << page;
    Page_wnd->layout()->addWidget(page);
    adjustScrollAreaWidgetSize();
}

void MyToolBox::setBarHeight(int index, int height)
{
    int iCount = PageBar.count();
    if (index < 0 || index >= iCount) return;

    PageBar[index]->setBarHeight(height);
    adjustScrollAreaWidgetSize();
}

void MyToolBox::setContentLayout(int index, QLayout* layout)
{
    int iCount = PageBar.count();
    if (index < 0 || index >= iCount) return;

    PageBar[index]->setContentLayout(layout);
}

void MyToolBox::Wnd_Resize()
{
    if (Page_wnd) {
        Area->setFixedSize(this->size());
        //renewLayout();
        for (int i = 0; i < PageBar.size(); i++)
            PageBar.at(i)->ResizePage();
        Page_wnd->adjustSize();
        Page_wnd->setFixedWidth(this->width());
    }
}

void MyToolBox::initVar()
{
	Area = NULL;
	Page_wnd = NULL;
}

void MyToolBox::initLayout()
{
    QVBoxLayout* Main_Layout = new QVBoxLayout;
    Main_Layout->setSpacing(4);
    Main_Layout->setContentsMargins(2, 2, 4, 2);
    for(int i=0;i<PageBar.size();i++)
    {
        Main_Layout->addWidget(PageBar.at(i));
    }
    //Main_Layout->addStretch();

    Page_wnd->setLayout(Main_Layout);
    adjustScrollAreaWidgetSize();
}

void MyToolBox::renewLayout()
{
    QVBoxLayout* Main_Layout = new QVBoxLayout;
    Main_Layout->setSpacing(4);
    Main_Layout->setContentsMargins(2, 2, 4, 2);
    for (int i = 0; i < PageBar.size(); i++)
    {
        Main_Layout->addWidget(PageBar.at(i));
    }
    //Main_Layout->addStretch();

    Page_wnd->setLayout(Main_Layout);
}

void MyToolBox::createScrollArea()
{
    //创建
    Page_wnd = new QWidget;
    Area = new QScrollArea(this);
    Area->setWidget(Page_wnd);
    Area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);    //不开启横向滚动
    Area->verticalScrollBar()->installEventFilter(this);           //给竖向滚动安装过滤器，因为在滚动条出现或者消失的时候需要调节m_pScrollAreaWidget大小
}

bool MyToolBox::eventFilter(QObject* watched, QEvent* event)
{
    //重新改变控件的显示大小
    switch ((int)event->type())
    {
    case QEvent::Resize:
        if (watched->inherits("MyToolBox"))
        {
            //根据当前是否有滚动条出现，如果有需要减去滚动条的宽度以免挡住Bar的图标
            bool b = Area->verticalScrollBar()->isVisible();
            int iScrollBarWidth = Area->verticalScrollBar()->width();

            if (Area) Area->setFixedSize(size());
            if (Page_wnd)
                Page_wnd->setFixedWidth(width() - (b ? iScrollBarWidth : 0));
        }
        break;

    case QEvent::Show:
    case QEvent::Hide:
        if (watched->inherits("QScrollBar"))
        {
            //在点击Bar显示/隐藏Contents 根据当前是否有滚动条出现，如果有需要减去滚动条的宽度以免挡住Bar的图标
            int iScrollBarWidth = Area->verticalScrollBar()->width();
            int iWidth = width() - ((event->type() == QEvent::Show) ? iScrollBarWidth : 0);
            if (Page_wnd) Page_wnd->setFixedWidth(iWidth);
        }
        break;
    }

    return QWidget::eventFilter(watched, event);
}