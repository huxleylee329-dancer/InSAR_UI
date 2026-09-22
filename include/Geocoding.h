#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Geocoding.h"
#include "GeocodingWorker.h"
#include <QPointer>

class Geocoding : public QWidget
{
    Q_OBJECT
public:
    explicit Geocoding(QWidget* parent = Q_NULLPTR);
    ~Geocoding();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
signals:
    void operate(int type, int multiRg, int multiAz, QString savePath, QStringList inputPaths,
        QString productLevel, int masterIndex, QString dstNode, QString demPath);
    void sendCopy(QStandardItemModel*);

private:
    Ui::Geocoding* ui = nullptr;
    QStandardItemModel* copy = nullptr;
    QPointer<GeocodingWorker> Geocoding_thread;
    QString save_path;
    QString projectFile;
    int image_number = 0;
    void ChangeVision(bool Editable);
    bool buildInputSnapshot(QStandardItem* project, const QString& srcNodeName, int type,
        const QString& projectPath, QStringList& inputPaths, QString& productLevel, int& masterIndex) const;
    void persistGeneratedResults();

    QString m_activeProjectName;
    QString m_activeProjectPath;
    QList<GeocodingFileResult> m_generatedResults;

    QLabel* m_demPathLabel1 = nullptr;
    QLineEdit* m_demPathEdit1 = nullptr;
    QPushButton* m_demBrowseBtn1 = nullptr;

    QLabel* m_demPathLabel2 = nullptr;
    QLineEdit* m_demPathEdit2 = nullptr;
    QPushButton* m_demBrowseBtn2 = nullptr;
private slots:
    /*工程选择按钮响应函数*/
    void on_comboBox_project1_currentIndexChanged();
    /*工程选择按钮响应函数*/
    void on_comboBox_project2_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_buttonBox_2_accepted();
    void on_buttonBox_2_rejected();
};
