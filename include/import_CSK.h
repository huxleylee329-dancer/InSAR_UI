#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_ImportCsk.h"
#include "CSKImportWorker.h"
#include <vector>

class import_CSK : public QWidget
{
    Q_OBJECT
public:
    explicit import_CSK(QWidget* parent = Q_NULLPTR);
    ~import_CSK();
public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    bool generate_name(QListWidget* imageslist, std::vector<QString>& original_nameslist, std::vector<QString>& import_nameslist);
    void ChangeVision(bool Editable);
private:
    Ui::ImportCsk* ui;
    QString save_path;
    QStandardItemModel* copy;
    CSKImportWorker* import_CSK_thread;
signals:
    void sendPath(QString, QString, QString);
    void operate2(QString, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);
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