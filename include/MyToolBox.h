#pragma once
#include <QtWidgets/QMainWindow>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>


class MyPage;
class MyToolBox : public QWidget
{
    Q_OBJECT
        enum { SPACE = 2, SPACEL = 2, SPACET = 2, SPACER = 4, SPACEB = 2 };
public:
    explicit MyToolBox(QWidget* parent = nullptr);
    ~MyToolBox();
    int count() const;
    MyPage* getItem(int index);
    void addItem(MyPage* page = 0);
    void setBarHeight(int index = 0, int height = 0);
    void setContentLayout(int index = 0, QLayout* layout = 0);
    inline void adjustScrollAreaWidgetSize() {
        if (Page_wnd) {
            Area->setFixedSize(this->size());
            //renewLayout();
            Page_wnd->adjustSize();
            Page_wnd->setFixedWidth(this->width());
        }
    }
    void Wnd_Resize();
    QSize ToolBox_size;

protected:
    void initVar();
    void initLayout();
    void renewLayout();
    void createScrollArea();

    bool eventFilter(QObject* watched, QEvent* event);

private:
    //¹ö¶¯ÊÓ´°
    QScrollArea* Area;
    QWidget* Page_wnd;

    //Bar
    QList<MyPage*> PageBar;
};
