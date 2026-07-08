#pragma once
#include <QtWidgets/QMainWindow>
#include <qstandarditemmodel.h>
#include "ui_SlcDeramp.h"

class SLCDerampWorker;
class QThread;

class SLC_deramp : public QWidget
{
    Q_OBJECT
public:
    explicit SLC_deramp(QWidget* parent = Q_NULLPTR);
    ~SLC_deramp();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
signals:
    void operate(int masterIndex, QString project_name, QString src_node, QString dst_node, QStandardItemModel* model, QString dem_path);
    void sendCopy(QStandardItemModel*);

private:
    Ui::SlcDeramp* ui;
    QStandardItemModel* copy;
    QThread* m_thread;
    SLCDerampWorker* m_worker;

    QString save_path;
    int method;
    int image_number;
    void ChangeVision(bool Editable);
    
    QLabel* m_demPathLabel = nullptr;
    QLineEdit* m_demPathEdit = nullptr;
    QPushButton* m_demBrowseBtn = nullptr;

private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_dst_node_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
};