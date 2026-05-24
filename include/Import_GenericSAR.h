#pragma once
#include <QtWidgets/QWidget>
#include <qstandarditemmodel.h>
#include "ui_ImportGenericSAR.h"
#include <QPointer>
#include "GenericSARImportTask.h"

class Import_GenericSAR : public QWidget
{
    Q_OBJECT

public:
    explicit Import_GenericSAR(QWidget* parent = Q_NULLPTR);
    ~Import_GenericSAR();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void ChangeVision(bool Editable);
    bool generate_name(QListWidget* imageslist, vector<QString>& original_nameslist, vector<QString>& import_nameslist);

private:
    Ui::ImportGenericSAR* ui;
    QString xml_path;
    QString save_path;
    QStandardItemModel* copy;
    QPointer<GenericSARImportTask> import_GenericSAR_thread;
    QPointer<GenericSARBatchImportTask> import_GenericSAR_thread2;

signals:
    void sendCopy(QStandardItemModel* model);


private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    void on_comboBox_dst_project_2_currentIndexChanged();
    void on_button_xml_browse_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_buttonBox_2_accepted();
    void on_buttonBox_2_rejected();
    void updateProcess(int, QString);
    void endProcess();
    void StopThread();
    void TransitModel(QStandardItemModel*);
    void on_pushButton_add_pressed();
    void on_pushButton_remove_pressed();

};
