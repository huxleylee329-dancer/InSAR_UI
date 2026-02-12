#ifndef NODEEDITORWINDOW_H
#define NODEEDITORWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QMenuBar>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <memory>

// QtNodes headers
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>

class NodeEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit NodeEditorWindow(QWidget *parent = nullptr);
    ~NodeEditorWindow();

private slots:
    void onNew();
    void onSave();
    void onLoad();
    void onClear();
    void onDelete();
    void onSceneModified(QtNodes::BasicGraphicsScene *);
    void onSceneLoaded();

private:
    void setupUi();
    void setupToolbar();
    void setupMenu();
    void setupSceneInternal();
    void applyStyles();
    QString getSaveFilePath();
    QString getOpenFilePath();

private:
    // UI components
    QVBoxLayout *m_layout;
    QToolBar *m_toolbar;
    QAction *m_actionNew;
    QAction *m_actionSave;
    QAction *m_actionLoad;
    QAction *m_actionClear;
    QAction *m_actionDelete;
    QAction *m_actionExit;

    // Node Editor components
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::DataFlowGraphicsScene *m_scene;
    QtNodes::GraphicsView *m_view;

    // State
    QString m_currentFilePath;
};

#endif // NODEEDITORWINDOW_H
