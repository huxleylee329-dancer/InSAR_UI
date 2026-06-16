#pragma once
#include <QtWidgets/QWidget>
#include <qstandarditemmodel.h>
#include "ui_ImportSpacety.h"
#include "SpacetyImportWorker.h"
#include <vector>

class import_Spacety : public QWidget
{
    Q_OBJECT
public:
    explicit import_Spacety(QWidget* parent = Q_NULLPTR);
    ~import_Spacety();
public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    bool generate_name(QListWidget* imageslist, std::vector<QString>& data_file_list, std::vector<QString>& xml_file_list, std::vector<QString>& import_nameslist);
    void ChangeVision(bool Editable);
private:
    Ui::ImportSpacety* ui;
    QString save_path;
    QStandardItemModel* copy;
    SpacetyImportWorker* import_Spacety_thread;
signals:
    void sendPath(QString, QString, QString);
    void operate2(QString, std::vector<QString>, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*, bool);
    void sendCopy(QStandardItemModel*);
private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    void on_pushButton_browse_data_pressed();
    void on_pushButton_browse_xml_pressed();
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
