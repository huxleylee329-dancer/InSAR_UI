#pragma once
#include <QtWidgets/QMainWindow>
#include <qstandarditemmodel.h>
#include "ui_ImportAirsat.h"
#include "AIRSATImportWorker.h"
#include <vector>

class import_AIRSAT : public QWidget
{
    Q_OBJECT
public:
    explicit import_AIRSAT(QWidget* parent = Q_NULLPTR);
    ~import_AIRSAT();

public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    bool generate_name(QListWidget* imageslist, std::vector<QString>& data_files, std::vector<QString>& xml_files, std::vector<QString>& import_names);
    void ChangeVision(bool Editable);

private:
    Ui::ImportAirsat* ui;
    QString save_path;
    QStandardItemModel* copy;
    AIRSATImportWorker* import_AIRSAT_thread;

signals:
    void sendPath(QString, QString, QString);
    void operate2(QString, std::vector<QString>, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);

private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    void on_pushButton_browse_data_clicked();
    void on_pushButton_browse_xml_clicked();
    void on_pushButton_add_pressed();
    void on_pushButton_remove_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
};
