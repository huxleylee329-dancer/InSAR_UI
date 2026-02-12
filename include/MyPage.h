#pragma once
#include <QLabel>
#include <QtWidgets/QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QRadioButton>
#include<QLineEdit>
#include<QPushButton>
#include <QListWidget>
#include"para_struct.h"
class MyToolBox;
class MyPage : public QWidget
{
    Q_OBJECT
        enum { BAR_HEIGHT = 50 };
    enum {Import = 1, Cut = 2, Regis = 3, IF = 4, Denoise = 5, Unwrap = 6, Dem = 7};
public:
    explicit MyPage(QWidget* parent = nullptr, int name = 0, int number = 0);

    inline void setExpand(bool expand) { m_bExpand = expand; stateSwitch(); }
    inline void setBarHeight(int height) { Bar->setFixedHeight(height); }
    inline void setContentLayout(QLayout* Layout = NULL) { Content->setLayout(Layout); }
    void ResizePage();
    /*传值函数*/
    bool get_param(template_DEM_para* parameter);
private:
    void createCtrl();
    void initLayout();
    void setIcon();
    void stateSwitch();
    QListWidget* Import_list=NULL;
    QPushButton* add=NULL;
    QPushButton* remove=NULL;
    QSpacerItem* verticalSpacer=NULL;
    QLineEdit* lon=NULL;
    QLineEdit* lat=NULL;
    QLineEdit* height=NULL;
    QLineEdit* width=NULL;
    QLineEdit* Index=NULL;
    QLineEdit* Interp=NULL;
    QLineEdit* Block=NULL;
    QLineEdit* IF_index=NULL;
    QCheckBox* IsDeflat=NULL;
    QCheckBox* Istopo=NULL;
    QCheckBox* Iscoh=NULL;
    QLabel* win_h_abel=NULL;
    QLineEdit* win_h=NULL;
    QLabel* win_w_label=NULL;
    QLineEdit* win_w=NULL;
    QLineEdit* multi_rg=NULL;
    QLineEdit* multi_az=NULL;
    //QGroupBox* groupBox=NULL;
    QHBoxLayout* horizontalLayout_6=NULL;
    QRadioButton* Slop_button=NULL;
    QRadioButton* Goldstein_button=NULL;
    QRadioButton* DL_button=NULL;
    QLabel* Slop_label=NULL;
    QLineEdit* Slop_win=NULL;
    QLabel* Pre_label=NULL;
    QLineEdit* Pre_win=NULL;
    QLabel* alpha_label=NULL;
    QLineEdit* alpha=NULL;
    QLabel* Goldstein_label=NULL;
    QLineEdit* Goldstein_win=NULL;
    QLabel* Goldstein_pad_label = NULL;
    QLineEdit* Goldstein_pad_win = NULL;
    //QGroupBox* groupBox_2=NULL;
    QRadioButton* SPD=NULL;
    QRadioButton* MCF=NULL;
    QRadioButton* SNAPHU=NULL;
    QRadioButton* Q_M=NULL;
    QLabel* threshold_label=NULL;
    QLineEdit* threshold=NULL;
    QWidget* DEM=NULL;
    //QGroupBox* groupBox_3=NULL;
    QRadioButton* Newton=NULL;
    QLabel* times_label=NULL;
    QLineEdit* iteration_times=NULL;
    QCheckBox* checkBox=NULL;
    //QDialogButtonBox* buttonBox=NULL;
private slots:
    void IF_Settings();
    void Denoise_Settings();
    void Unwrap_Settings();
    void DEM_Settings();
    void addimage();
    void removeimage();
protected:
    void mousePressEvent(QMouseEvent* event);

private:
    bool m_bExpand;
    int process_index;        //步骤名称
   
    QLabel* Name;       //标题名称
    QLabel* Icon;       //图标
    QWidget* Bar;        //标题栏
    QWidget* Content;    //内容区

    MyToolBox* ToolBox;  //父窗口
    int page_number;
};
