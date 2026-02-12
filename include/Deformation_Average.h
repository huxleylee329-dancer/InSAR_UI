#pragma once
#include <QtWidgets/QMainWindow>
#include<FormatConversion.h>
#include "ui_Deformation_Average.h"
#include"MyThread.h"
#include "Deformation_Preview_Window.h"
#include"ImageView.h"
#include<Qtgui>

class Deformation_Average : public QWidget
{
    Q_OBJECT
public:
    explicit Deformation_Average(QWidget* parent = Q_NULLPTR);
    ~Deformation_Average();
    QStandardItemModel* copy;
public slots:
    void ShowProjectList(QStandardItemModel*);
    void TransitModel(QStandardItemModel*);
    void cancelled();

private:
    double h5_left, h5_right, h5_top, h5_bottom;
    /*若已经点击过预览，则禁止再次点击*/
    bool isPreviewPressed;
    /*是否正在裁剪*/
    bool isDeformation_Averageting;
    Ui::Deformation_Average* ui;
    QString save_path;
    MyThread* Deformation_Average_thread;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void sendCopy(QStandardItemModel*);
private slots:
    /*经纬度裁剪下拉框*/
    void on_comboBox_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();

    void on_Preview_pressed();

};