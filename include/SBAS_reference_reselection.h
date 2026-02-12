#pragma once
#include <QtWidgets/QMainWindow>
#include<FormatConversion.h>
#include"ui_SBAS_reference_reselection.h"
#include"MyThread.h"
#include"reselection_view.h"
#include"ImageView.h"
#include<Qtgui>

class SBAS_reference_reselection : public QWidget
{
    Q_OBJECT
public:
    explicit SBAS_reference_reselection(QWidget* parent = Q_NULLPTR);
    ~SBAS_reference_reselection();
    QStandardItemModel* copy;
public slots:
    void ShowProjectList(QStandardItemModel*);
    void TransitModel(QStandardItemModel*);
    void cancelled();
    void receive_coordinate(int, int, QList<QPoint>);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
private:
    double h5_left, h5_right, h5_top, h5_bottom;
    /*若已经点击过预览，则禁止再次点击*/
    bool isReselectionPressed;
    /*是否正在选择参考点*/
    bool isSBAS_reference_reselection;
    Ui::SBAS_reference_reselection* ui;
    QString save_path;
    MyThread* SBAS_reference_reselection_thread;
    int image_number;
    int ref_row, ref_col;
    QList<QPoint> plist;
    void ChangeVision(bool Editable);
signals:
    void sendCopy(QStandardItemModel*);
    void operate(QString, QString, int, int, QList<QPoint>, QStandardItemModel*);
private slots:

    void on_comboBox_project_currentIndexChanged();
    void on_comboBox_srcNode_currentIndexChanged();

    void on_reselection_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

};