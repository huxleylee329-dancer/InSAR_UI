#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_ImportAlos2.h"
#include "ALOS2ImportWorker.h"
#include <vector>

class import_ALOS2 : public QWidget
{
    Q_OBJECT
public:
    explicit import_ALOS2(QWidget* parent = Q_NULLPTR);
    ~import_ALOS2();
public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    bool generate_name(QListWidget* imageslist, std::vector<QString>& original_nameslist, std::vector<QString>& import_nameslist,
        std::vector<QString>& original_nameslist2);
    void ChangeVision(bool Editable);
private:
    Ui::ImportAlos2* ui;
    QString save_path;
    QStandardItemModel* copy;
    ALOS2ImportWorker* import_ALOS2_thread;
signals:
    void sendPath(QString, QString, QString);
    void operate2(QString, std::vector<QString>, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    void on_pushButton_add_pressed();
    /*批量导入移除按钮响应函数*/
    void on_pushButton_remove_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
};