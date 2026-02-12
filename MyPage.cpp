#include "MyPage.h"
#include "MyToolBox.h"
#include <QListWidget>
#include <QPushButton>
#include <QMouseEvent>
#include <QLineEdit>
#include <QGroupBox>
#include <QFileDialog>
#include <qstandarditemmodel.h>

MyPage::MyPage(QWidget* parent, int name, int number) : QWidget(parent)
{
    //初始化变量
    ToolBox = (MyToolBox*)parent;
    page_number = number;
    m_bExpand = false;
    process_index = name;

    //创建标题栏、内容区、名称、图标
    createCtrl();

    //初始化布局
    initLayout();
    if (name == Import);
    {
        connect(add, &QPushButton::clicked, this, &MyPage::addimage);
        connect(remove, &QPushButton::clicked, this, &MyPage::removeimage);
    }
    if (name == IF)
    {
        connect(Iscoh, &QCheckBox::stateChanged, this, &MyPage::IF_Settings);
    }
    else if (name == Denoise)
    {
        connect(Slop_button, &QRadioButton::clicked, this, &MyPage::Denoise_Settings);
        connect(Goldstein_button, &QRadioButton::clicked, this, &MyPage::Denoise_Settings);
        connect(DL_button, &QRadioButton::clicked, this, &MyPage::Denoise_Settings);
    }
    else if (name == Unwrap)
    {
        connect(SPD, &QRadioButton::clicked, this, &MyPage::Unwrap_Settings);
        connect(MCF, &QRadioButton::clicked, this, &MyPage::Unwrap_Settings);
        connect(SNAPHU, &QRadioButton::clicked, this, &MyPage::Unwrap_Settings);
        connect(Q_M, &QRadioButton::clicked, this, &MyPage::Unwrap_Settings);
    }
    else if (name == Dem)
    {
        connect(Newton, &QRadioButton::clicked, this, &MyPage::DEM_Settings);
    }
    
    
    
   
}

void MyPage::ResizePage()
{
    this->adjustSize();
    this->setFixedWidth(ToolBox->width());
    ToolBox->adjustScrollAreaWidgetSize();
}

bool MyPage::get_param(template_DEM_para* parameter)
{
    int ret;
    if (!parameter) return false;
    switch (process_index)
    {
    default:
        break;
    case Import:
        if (Import_list->count() == 2)
        {
            strcpy(parameter->master_image_file, Import_list->item(0)->text().toStdString().c_str());
            strcpy(parameter->slave_image_file, Import_list->item(1)->text().toStdString().c_str());
        }
        else
        {
            return false;
        }
        break;
    case Cut:
        ret = sscanf(this->lat->text().toStdString().c_str(), "%lf", &parameter->AOI_center_lat);
        if (ret != 1)
        {
            parameter->AOI_center_lat = 181.0;
        }
        ret = sscanf(this->lon->text().toStdString().c_str(), "%lf", &parameter->AOI_center_lon);
        if (ret != 1)
        {
            parameter->AOI_center_lon = 181.0;
        }
        ret = sscanf(this->width->text().toStdString().c_str(), "%lf", &parameter->AOI_width);
        if (ret != 1)
        {
            parameter->AOI_width = 5000;
        }
        ret = sscanf(this->height->text().toStdString().c_str(), "%lf", &parameter->AOI_height);
        if (ret != 1)
        {
            parameter->AOI_height = 5000;
        }
        break;

    case Regis:
        ret = sscanf(this->Interp->text().toStdString().c_str(), "%d", &parameter->coregis_interp_times);
        if (ret != 1)
        {
            parameter->coregis_interp_times = 8;
        }
        ret = sscanf(this->Block->text().toStdString().c_str(), "%d", &parameter->coregis_blocksize);
        if (ret != 1)
        {
            parameter->coregis_blocksize = 256;
        }
        break;
    case IF:
        parameter->ifg_b_coh = this->Iscoh->isChecked();
        ret = sscanf(this->win_h->text().toStdString().c_str(), "%d", &parameter->ifg_coh_wndsize_az);
        if (ret != 1)
        {
            parameter->ifg_coh_wndsize_az = 3;
        }
        ret = sscanf(this->win_w->text().toStdString().c_str(), "%d", &parameter->ifg_coh_wndsize_rg);
        if (ret != 1)
        {
            parameter->ifg_coh_wndsize_rg = 3;
        }
        ret = sscanf(this->multi_rg->text().toStdString().c_str(), "%d", &parameter->ifg_multilook_rg);
        if (ret != 1)
        {
            parameter->ifg_multilook_rg = 1;
        }
        ret = sscanf(this->multi_az->text().toStdString().c_str(), "%d", &parameter->ifg_multilook_az);
        if (ret != 1)
        {
            parameter->ifg_multilook_az = 1;
        }
        break;
    case Denoise:
        if (this->Slop_button->isChecked())
        {
            parameter->denoise_method = 1;
            ret = sscanf(this->Pre_win->text().toStdString().c_str(), "%d", &parameter->slope_pre_size);
            if (ret != 1) parameter->slope_pre_size = 11;
            ret = sscanf(this->Slop_win->text().toStdString().c_str(), "%d", &parameter->slope_size);
            if (ret != 1) parameter->slope_size = 11;
        }
        else if (this->DL_button->isChecked())
        {
            parameter->denoise_method = 3;
        }
        else
        {
            parameter->denoise_method = 2;
            ret = sscanf(this->Goldstein_win->text().toStdString().c_str(), "%d", &parameter->Goldstein_FFT_wdnsize);
            ret = sscanf(this->Goldstein_pad_win->text().toStdString().c_str(), "%d", &parameter->Goldstein_pad);
            ret = sscanf(this->alpha->text().toStdString().c_str(), "%lf", &parameter->Goldstein_alpha);
        }
        break;
    case Unwrap:
        if (this->SPD->isChecked())parameter->unwrap_method = 1;
        else if (this->MCF->isChecked()) parameter->unwrap_method = 2;
        else if (this->SNAPHU->isChecked()) parameter->unwrap_method = 3;
        else
        {
            parameter->unwrap_method = 4;
            ret = sscanf(this->threshold->text().toStdString().c_str(), "%lf", &parameter->unwrap_coh_thresh);
        }
        break;
    case Dem:
        if (this->Newton->isChecked())
        {
            ret = sscanf(this->iteration_times->text().toStdString().c_str(), "%d", &parameter->iter_times);
        }
        else
        {
            parameter->iter_times = 20;
        }
        break;
    }
    return true;
}

void MyPage::createCtrl()
{
    //标题栏
    Bar = new QWidget(this);
    Bar->setMinimumHeight(BAR_HEIGHT);
    Bar->setStyleSheet("background: rgb(170, 170, 170)");
    switch (process_index)
    {
    case Import:
    {
        Name = new QLabel(QString::fromLocal8Bit("导入数据"), Bar);
        //Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
       
        Import_list = new QListWidget;
        //Import_list->adjustSize();
        add = new QPushButton("add");
       // Add_button->adjustSize();
        remove = new QPushButton("remove");
        //Remove_button->adjustSize();
        QHBoxLayout* Hlayout = new QHBoxLayout;
        QVBoxLayout* Vlayout = new QVBoxLayout;
        Vlayout->addWidget(add);
        Vlayout->addWidget(remove);
        Vlayout->setAlignment(Qt::AlignCenter);
        Hlayout->addWidget(Import_list);
        Hlayout->addLayout(Vlayout);
        Content->setLayout(Hlayout);
        Content->hide();
        break;
    }
    case Cut:
    {
        Name = new QLabel(QString::fromLocal8Bit("处理区域中心坐标"), Bar);
        Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
        QGridLayout* Layout = new QGridLayout;
        QLabel* lon_label = new QLabel(QString::fromLocal8Bit("经度(°)："));
        lon_label->adjustSize();
        QLabel* lat_label = new QLabel(QString::fromLocal8Bit("纬度(°)："));
        lat_label->adjustSize();
        QLabel* height_label = new QLabel(QString::fromLocal8Bit("高度(m)："));
        height_label->adjustSize();
        QLabel* width_label = new QLabel(QString::fromLocal8Bit("宽度(m)："));
        width_label->adjustSize();
        lon = new QLineEdit;
        lon->adjustSize();
        lon->setPlaceholderText("(-180.0, 180.0)");
        lat = new QLineEdit;
        lat->adjustSize();
        lat->setPlaceholderText("(-90.0, 90.0)");
        height = new QLineEdit;
        height->adjustSize();
        height->setPlaceholderText("5000");
        width = new QLineEdit;
        width->adjustSize();
        width->setPlaceholderText("5000");
        Layout->addWidget(lon_label, 0, 0);
        Layout->addWidget(lon, 0, 1);
        Layout->addWidget(lat_label, 0, 2);
        Layout->addWidget(lat, 0, 3);
        Layout->addWidget(height_label, 1, 0);
        Layout->addWidget(height, 1, 1);
        Layout->addWidget(width_label, 1, 2);
        Layout->addWidget(width, 1, 3);
        Content->setLayout(Layout);
        Content->hide();
        break;
    }
    case Regis:
    {
        Name = new QLabel(QString::fromLocal8Bit("图像配准"), Bar);
        Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
        QGridLayout* Layout = new QGridLayout;
        //QLabel* index_label = new QLabel(QString::fromLocal8Bit("主图像序号(1-n)："));
        //index_label->adjustSize();
        //Index = new QLineEdit;
        //Index->adjustSize();
        QLabel* interp_label = new QLabel(QString::fromLocal8Bit("插值倍数(2^n)："));
        interp_label->adjustSize();
        Interp = new QLineEdit;
        Interp->adjustSize();
        Interp->setPlaceholderText("8");
        QLabel* block_label = new QLabel(QString::fromLocal8Bit("子块尺寸(2^n)："));
        block_label->adjustSize();
        Block = new QLineEdit;
        Block->adjustSize();
        Block->setPlaceholderText("256");

        //Layout->addWidget(index_label, 0, 0);
        //Layout->addWidget(Index, 0, 1);
        Layout->addWidget(interp_label, 0, 0);
        Layout->addWidget(Interp, 0, 1);
        Layout->addWidget(block_label, 1, 0);
        Layout->addWidget(Block, 1, 1);
        Content->setLayout(Layout);
        Content->hide();
        break;
    }
    case IF:
    {
        Name = new QLabel(QString::fromLocal8Bit("干涉相位生成"), Bar);
        Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
        QGridLayout* Layout = new QGridLayout;
        //QLabel* IFindex_label = new QLabel(QString::fromLocal8Bit("主图像序号(1-n)："));
        //IFindex_label->adjustSize();
        //IF_index = new  QLineEdit;
        //IF_index->adjustSize();
        //IsDeflat = new QCheckBox(QString::fromLocal8Bit("是否去平地"));
        //IsDeflat->adjustSize();
        //Istopo = new QCheckBox(QString::fromLocal8Bit("是否去地形"));
        //Istopo->adjustSize();
        Iscoh = new QCheckBox(QString::fromLocal8Bit("是否计算相干系数"));
        Iscoh->adjustSize();
        Iscoh->setChecked(true);
        win_h_abel = new QLabel(QString::fromLocal8Bit("相干系数估计窗口高度："));
        win_h_abel->adjustSize();
        win_h = new QLineEdit;
        win_h->adjustSize();
        win_h->setPlaceholderText("3");
        win_w_label = new QLabel(QString::fromLocal8Bit("相干系数估计窗口宽度："));
        win_w_label->adjustSize();
        win_w = new QLineEdit;
        win_w->adjustSize();
        win_w->setPlaceholderText("3");
        //win_h_abel->hide();
        //win_h->hide();
        //win_w_label->hide();
        //win_w->hide();
        QLabel* Multi_az_label = new QLabel(QString::fromLocal8Bit("多视倍数(方位向)："));
        Multi_az_label->adjustSize();
        multi_az = new  QLineEdit;
        multi_az->adjustSize();
        multi_az->setPlaceholderText("1");
        QLabel* Multi_rg_label = new QLabel(QString::fromLocal8Bit("多视倍数(距离向)："));
        Multi_rg_label->adjustSize();
        multi_rg = new  QLineEdit;
        multi_rg->adjustSize();
        multi_rg->setPlaceholderText("1");
        //Layout->addWidget(IFindex_label, 0, 0);
        //Layout->addWidget(IF_index, 0, 1);
        //Layout->addWidget(IsDeflat, 1, 0);
        //Layout->addWidget(Istopo, 2, 0);
        Layout->addWidget(Iscoh, 0, 0);
        Layout->addWidget(win_h_abel, 1, 0);
        Layout->addWidget(win_h, 1, 1);
        Layout->addWidget(win_w_label, 1, 2);
        Layout->addWidget(win_w, 1, 3);
        Layout->addWidget(Multi_az_label, 2, 0);
        Layout->addWidget(multi_az, 2, 1);
        Layout->addWidget(Multi_rg_label, 2, 2);
        Layout->addWidget(multi_rg, 2, 3);
        Content->setLayout(Layout);
        Content->hide();
        break;
    }
    case Denoise:
    {
        Name = new QLabel(QString::fromLocal8Bit("相位滤波"), Bar);
        Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
        QGridLayout* Layout = new QGridLayout;
        QGroupBox* Denoise_box = new QGroupBox(QString::fromLocal8Bit("滤波方法"));
        Slop_button = new QRadioButton(QString::fromLocal8Bit("斜坡自适应"));
        Slop_button->adjustSize();
        Goldstein_button = new QRadioButton(QString::fromLocal8Bit("Goldstein滤波"));
        Goldstein_button->adjustSize();
        Goldstein_button->setChecked(true);
        DL_button = new QRadioButton(QString::fromLocal8Bit("深度学习滤波"));
        DL_button->adjustSize();
        QHBoxLayout* Box_Layout = new QHBoxLayout;
        Box_Layout->addWidget(Slop_button);
        Box_Layout->addWidget(Goldstein_button);
        Box_Layout->addWidget(DL_button);
        Denoise_box->setLayout(Box_Layout);
        Slop_label = new QLabel(QString::fromLocal8Bit("斜坡自适应窗口尺寸："));
        Slop_label->adjustSize();
        Slop_label->hide();
        Slop_win = new QLineEdit;
        Slop_win->adjustSize();
        Slop_win->setPlaceholderText("11");
        Slop_win->hide();
        Pre_label = new QLabel(QString::fromLocal8Bit("预滤波窗口尺寸："));
        Pre_label->adjustSize();
        Pre_label->hide();
        Pre_win = new QLineEdit;
        Pre_win->adjustSize();
        Pre_win->setPlaceholderText("11");
        Pre_win->hide();
        QHBoxLayout* Slop_layout = new QHBoxLayout;
        Slop_layout->addWidget(Slop_label);
        Slop_layout->addWidget(Slop_win);
        Slop_layout->addWidget(Pre_label);
        Slop_layout->addWidget(Pre_win);
        Goldstein_label = new QLabel(QString::fromLocal8Bit("Goldstein滤波窗尺寸："));
        Goldstein_label->adjustSize();
        //Goldstein_label->hide();
        Goldstein_pad_label = new QLabel(QString::fromLocal8Bit("Goldstein滤波补零尺寸："));
        Goldstein_pad_label->adjustSize();
        //Goldstein_pad_label->hide();
        Goldstein_win = new QLineEdit;
        Goldstein_win->adjustSize();
        Goldstein_win->setPlaceholderText("64");
        //Goldstein_win->hide();
        Goldstein_pad_win = new QLineEdit;
        Goldstein_pad_win->adjustSize();
        Goldstein_pad_win->setPlaceholderText("8");
        //Goldstein_pad_win->hide();
        alpha_label = new QLabel(QString::fromLocal8Bit("alpha："));
        alpha_label, adjustSize();
        //alpha_label->hide();
        alpha = new QLineEdit;
        alpha->adjustSize();
        alpha->setPlaceholderText("0.9");
        //alpha->hide();
        QHBoxLayout* Goldstein_layout = new QHBoxLayout;
        Goldstein_layout->addWidget(Goldstein_label);
        Goldstein_layout->addWidget(Goldstein_win);
        Goldstein_layout->addWidget(Goldstein_pad_label);
        Goldstein_layout->addWidget(Goldstein_pad_win);
        Goldstein_layout->addWidget(alpha_label);
        Goldstein_layout->addWidget(alpha);
        Layout->addWidget(Denoise_box, 0, 0);
        Layout->addLayout(Slop_layout,1,0);
        Layout->addLayout(Goldstein_layout, 2, 0);
        Content->setLayout(Layout);
        Content->hide();
        break;
    }
    case Unwrap:
    {
        Name = new QLabel(QString::fromLocal8Bit("相位解缠"), Bar);
        Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
        QGridLayout* Layout = new QGridLayout;
        QGroupBox* Unwrap_box = new QGroupBox(QString::fromLocal8Bit("解缠方法"));
        SPD = new QRadioButton(QString::fromLocal8Bit("质量图法"));
        SPD->adjustSize();
        MCF = new QRadioButton(QString::fromLocal8Bit("最小费用流"));
        MCF->adjustSize();
        MCF->setChecked(true);
        SNAPHU = new QRadioButton(QString::fromLocal8Bit("SNAPHU"));
        SNAPHU->adjustSize();
        Q_M = new QRadioButton(QString::fromLocal8Bit("综合法"));
        Q_M->adjustSize();
        QHBoxLayout* Box_Layout = new QHBoxLayout;
        Box_Layout->addWidget(SPD);
        Box_Layout->addWidget(MCF);
        Box_Layout->addWidget(SNAPHU);
        Box_Layout->addWidget(Q_M);
        Unwrap_box->setLayout(Box_Layout);
        threshold_label = new QLabel(QString::fromLocal8Bit("相干系数阈值："));
        threshold_label->adjustSize();
        threshold_label->hide();
        threshold = new QLineEdit;
        threshold->adjustSize();
        threshold->setPlaceholderText("0.7");
        threshold->hide();
        QHBoxLayout* QM_layout = new QHBoxLayout;
        QM_layout->addWidget(threshold_label);
        QM_layout->addWidget(threshold);
        Layout->addWidget(Unwrap_box, 0, 0);
        Layout->addLayout(QM_layout, 1, 0);
        Content->setLayout(Layout);
        Content->hide();
        break;
    }
    case Dem:
    {
        Name = new QLabel(QString::fromLocal8Bit("高程反演"), Bar);
        Name->adjustSize();

        Content = new QWidget(this);
        Content->setStyleSheet("background: rgb(218, 222, 225)");
        QGridLayout* Layout = new QGridLayout;
        QGroupBox* DEM_box = new QGroupBox(QString::fromLocal8Bit("DEM方法"));
        Newton = new QRadioButton(QString::fromLocal8Bit("牛顿法"));
        Newton->adjustSize();
        Newton->setChecked(true);
        QHBoxLayout* Box_Layout = new QHBoxLayout;
        Box_Layout->addWidget(Newton);
        DEM_box->setLayout(Box_Layout);
        times_label = new QLabel(QString::fromLocal8Bit("迭代次数："));
        times_label->adjustSize();
        times_label->hide();
        iteration_times= new QLineEdit;
        iteration_times->adjustSize();
        iteration_times->setPlaceholderText("20");
        //iteration_times->hide();
        QHBoxLayout* Newton_layout = new QHBoxLayout;
        Newton_layout->addWidget(times_label);
        Newton_layout->addWidget(iteration_times);
        Layout->addWidget(DEM_box, 0, 0);
        Layout->addLayout(Newton_layout, 1, 0);
        Content->setLayout(Layout);
        Content->hide();
        break;
    }
    default:
        break;
    }
}

void MyPage::initLayout()
{
    //标题栏布局
    QHBoxLayout* pBarLayout = new QHBoxLayout;
    pBarLayout->addWidget(Name);
    pBarLayout->addStretch();
    //pBarLayout->addWidget(m_pIcon);
    pBarLayout->setSpacing(0);
    pBarLayout->setContentsMargins(10, 0, 4, 0);
    Bar->setLayout(pBarLayout);

    //整体布局
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    pMainLayout->addWidget(Bar);
    pMainLayout->addWidget(Content);
    pMainLayout->addStretch();
    pMainLayout->setSpacing(0);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    this->setLayout(pMainLayout);
}

void MyPage::setIcon()
{
    //图标
    //QPixmap pixmap(m_bExpand ? ":/Others/Icon/Others/top.png" : ":/Others/Icon/Others/bottom.png");
    //Icon->setPixmap(pixmap);
}

void MyPage::stateSwitch()
{
    //图标
    setIcon();

    //状态
    Content->setVisible(m_bExpand);

    //自适应高度
    this->adjustSize();
    this->setFixedWidth(ToolBox->width());
    ToolBox->adjustScrollAreaWidgetSize();
}

void MyPage::mousePressEvent(QMouseEvent* event)
{
    QPoint point = event->pos();
    if (Bar->geometry().contains(point))
    {
        m_bExpand = !m_bExpand;
        stateSwitch();
    }

    QWidget::mousePressEvent(event);
}

void MyPage::IF_Settings()
{
    if (Iscoh->isChecked())
    {
        win_h->setHidden(0);
        win_h_abel->setHidden(0);
        win_w->setHidden(0);
        win_w_label->setHidden(0);
    }
    else
    {
        win_h->setHidden(1);
        win_h_abel->setHidden(1);
        win_w->setHidden(1);
        win_w_label->setHidden(1);
    }
    this->adjustSize();
    this->setFixedWidth(ToolBox->width());
    ToolBox->adjustScrollAreaWidgetSize();
}

void MyPage::Denoise_Settings()
{
    if (Slop_button->isChecked())
    {
        Goldstein_label->setHidden(1);
        Goldstein_win->setHidden(1);
        Goldstein_pad_label->setHidden(1);
        Goldstein_pad_win->setHidden(1);
        alpha->setHidden(1);
        alpha_label->setHidden(1);
        Slop_label->setHidden(0);
        Slop_win->setHidden(0);
        Pre_label->setHidden(0);
        Pre_win->setHidden(0);
    }
    else if (Goldstein_button->isChecked())
    {
        Goldstein_label->setHidden(0);
        Goldstein_win->setHidden(0);
        Goldstein_pad_label->setHidden(0);
        Goldstein_pad_win->setHidden(0);
        alpha->setHidden(0);
        alpha_label->setHidden(0);
        Slop_label->setHidden(1);
        Slop_win->setHidden(1);
        Pre_label->setHidden(1);
        Pre_win->setHidden(1);
    }
    else
    {
        Goldstein_label->setHidden(1);
        Goldstein_win->setHidden(1);
        Goldstein_pad_label->setHidden(1);
        Goldstein_pad_win->setHidden(1);
        alpha->setHidden(1);
        alpha_label->setHidden(1);
        Slop_label->setHidden(1);
        Slop_win->setHidden(1);
        Pre_label->setHidden(1);
        Pre_win->setHidden(1);
    }
    this->adjustSize();
    this->setFixedWidth(ToolBox->width());
    ToolBox->adjustScrollAreaWidgetSize();
}

void MyPage::Unwrap_Settings()
{
    if (Q_M->isChecked())
    {
        threshold_label->setHidden(0);
        threshold->setHidden(0);
    }
    else
    {
        threshold_label->setHidden(1);
        threshold->setHidden(1);
    }
    this->adjustSize();
    this->setFixedWidth(ToolBox->width());
    ToolBox->adjustScrollAreaWidgetSize();
}

void MyPage::DEM_Settings()
{
    if (Newton->isChecked())
    {
        times_label->setHidden(0);
        iteration_times->setHidden(0);
    }
    else
    {
        times_label->setHidden(1);
        iteration_times->setHidden(1);
    }
    this->adjustSize();
    this->setFixedWidth(ToolBox->width());
    ToolBox->adjustScrollAreaWidgetSize();
}

void MyPage::addimage()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QString::fromLocal8Bit("加入原始数据"),
        "",
        "file(*)");
    Import_list->addItem(filename);
}

void MyPage::removeimage()
{
    Import_list->takeItem(Import_list->currentRow());
}
