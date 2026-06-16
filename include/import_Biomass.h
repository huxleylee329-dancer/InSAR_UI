#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_ImportBiomass.h"
#include "BiomassImportWorker.h"
#include <vector>

class import_Biomass : public QWidget
{
    Q_OBJECT
public:
    explicit import_Biomass(QWidget* parent = nullptr);
    ~import_Biomass();

public slots:
    void errorProcess(QString error_msg);
    void ShowProjectList(QStandardItemModel*);
    void ChangeVision(bool Editable);

private:
    Ui::ImportBiomass* ui;
    QString save_path;
    QStandardItemModel* copy;
    BiomassImportWorker* import_Biomass_thread;

    // Parallel vectors for batch import items in the file list
    std::vector<QString> m_ampFiles;
    std::vector<QString> m_phaseFiles;
    std::vector<QString> m_xmlFiles;
    std::vector<QString> m_orbitFiles;
    std::vector<QString> m_polarizations;
    std::vector<QString> m_importNamelist;

signals:
    void operate(
        QString savepath,
        std::vector<QString> amp_files,
        std::vector<QString> phase_files,
        std::vector<QString> xml_files,
        std::vector<QString> orbit_files,
        std::vector<QString> polarizations,
        std::vector<QString> import_namelist,
        QString dst_node,
        QString dst_project,
        QStandardItemModel* model
    );
    void sendCopy(QStandardItemModel*);

private slots:
    void on_comboBox_dst_project_currentIndexChanged();
    
    // Browse slots
    void on_pushButton_amp_browse_pressed();
    void on_pushButton_phase_browse_pressed();
    void on_pushButton_xml_browse_pressed();
    void on_pushButton_orbit_browse_pressed();

    // List management slots
    void on_pushButton_add_pressed();
    void on_pushButton_remove_pressed();

    // Button box slots
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

    // Worker coordination slots
    void updateProcess(int value, QString information);
    void endProcess();
    void StopThread();
    void TransitModel(QStandardItemModel*);
};
