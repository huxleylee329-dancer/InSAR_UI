#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include <QListWidget>
#include "ui_ImportLidar.h"
#include "LidarImportWorker.h"
#include <vector>

class import_LiDAR : public QWidget
{
    Q_OBJECT
public:
    explicit import_LiDAR(QWidget* parent = Q_NULLPTR);
    ~import_LiDAR();

public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    bool generate_name(QListWidget* imageslist, std::vector<QString>& original_nameslist, std::vector<QString>& import_nameslist);
    void ChangeVision(bool Editable);

private:
    Ui::ImportLidar* ui;
    QString save_path;
    QStandardItemModel* copy;
    LidarImportWorker* import_lidar_thread;

signals:
    void sendPath(QString, QString, QString);
    void operate2(
        QString savepath,
        std::vector<QString> original_file_list,
        std::vector<QString> import_namelist,
        QString product_type,
        int rh_percentile,
        QString dst_node,
        QString dst_project,
        QStandardItemModel* model
    );
    void sendCopy(QStandardItemModel*);

private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    void on_pushButton_add_pressed();
    void on_pushButton_remove_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
    void on_comboBox_product_type_currentIndexChanged(int index);
};
