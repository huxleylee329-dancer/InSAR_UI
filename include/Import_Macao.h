#pragma once
#include <QtWidgets/QWidget>
#include <qstandarditemmodel.h>
#include "ui_ImportMacao.h"
#include "MyThread.h"

class Import_Macao : public QWidget
{
    Q_OBJECT

public:
    explicit Import_Macao(QWidget* parent = Q_NULLPTR);
    ~Import_Macao();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void ChangeVision(bool Editable);
    bool generate_name(QListWidget* imageslist, vector<QString>& original_nameslist, vector<QString>& import_nameslist);

private:
    Ui::ImportMacao* ui;
    QString xml_path;
    QString save_path;
    QStandardItemModel* copy;
    MyThread* import_Macao_thread, *import_Macao_thread2;

signals:
    void sendCopy(QStandardItemModel* model);
    void operate(QString, QString, QString, QString, QString, QStandardItemModel*);
    void operate2(QString, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

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
