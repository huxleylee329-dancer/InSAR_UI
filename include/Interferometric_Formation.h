#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Interferometric_Formation.h"
#include"MyThread.h"

class Interferometric_Formation : public QWidget
{
    Q_OBJECT
public:
    explicit Interferometric_Formation(QWidget* parent = Q_NULLPTR);
    ~Interferometric_Formation();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::Interferometric_Formation* ui;
    QStandardItemModel* copy;
    MyThread* Interferometric_Formation_thread;
    QString save_path;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(bool isdeflat, bool istopo_removal, bool iscoherence, int master_index, int win_r, int win_c, int multilook_rg, int multilook_az, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void sendCopy(QStandardItemModel*);
private slots:
    void ChangeSetting();
    //void on_iscoherence_Checked();
    void on_comboBox_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();
    
    // void on_masterpushButton_pressed();
     //void on_slavepushButton_pressed()
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
};