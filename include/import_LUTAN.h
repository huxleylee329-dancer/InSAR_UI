#pragma once
#include <QtWidgets/QWidget>
#include <qstandarditemmodel.h>
#include "ui_ImportLutan.h"
#include "LUTANImportWorker.h"
#include <vector>

class import_LUTAN : public QWidget
{
    Q_OBJECT
public:
    explicit import_LUTAN(QWidget* parent = Q_NULLPTR);
    ~import_LUTAN();

public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    void ChangeVision(bool Editable);

private:
    Ui::ImportLutan* ui;
    QString save_path;
    QStandardItemModel* copy;
    LUTANImportWorker* import_LUTAN_thread;

signals:
    void sendPath(QString, QString, QString);
    void operate2(QString savepath, std::vector<QString> data_files, std::vector<QString> xml_files, std::vector<int> modes, std::vector<QString> import_names, QString dst_node, QString dst_project, QStandardItemModel* model);
    void sendCopy(QStandardItemModel*);

private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    void on_pushButton_data_browse_pressed();
    void on_pushButton_xml_browse_pressed();
    void on_pushButton_add_pressed();
    void on_pushButton_remove_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

    void updateProcess(int value, QString info);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel* model);
};
