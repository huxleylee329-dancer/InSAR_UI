#include "NodeEditorWindow.h"
#include "NodeModels.h"
#include "TestNodeModels.h"

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>

#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <QLabel>
#include <memory>
#include <exception>

NodeEditorWindow::NodeEditorWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_layout(nullptr)
    , m_toolbar(nullptr)
    , m_actionNew(nullptr)
    , m_actionSave(nullptr)
    , m_actionLoad(nullptr)
    , m_actionClear(nullptr)
    , m_actionDelete(nullptr)
    , m_actionExit(nullptr)
    , m_graphModel(nullptr)
    , m_scene(nullptr)
    , m_view(nullptr)
{
    setWindowTitle("InSAR Node Editor");
    resize(1200, 800);

    setupUi();
    setupToolbar();
    setupMenu();

    // Initialize registry immediately
    try {
        m_registry = QtNodes::registerTestNodeModels();
        qDebug() << "Registry created";
    } catch (const std::exception& e) {
        qCritical() << "Exception during registry creation:" << e.what();
        return;
    }

    // Create a placeholder widget until scene is ready
    auto *placeholder = new QLabel("Node Editor Initializing...", m_layout->widget());
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("font-size: 16px; color: #666;");
    m_layout->addWidget(placeholder);

    // Defer full scene creation
    QTimer::singleShot(100, this, [this, placeholder]() {
        qDebug() << "Attempting delayed scene creation (100ms)...";
        setupSceneInternal();
        placeholder->deleteLater();
    });

    applyStyles();
}

NodeEditorWindow::~NodeEditorWindow()
{
    delete m_view;
    delete m_scene;
    delete m_graphModel;
}

void NodeEditorWindow::setupSceneInternal()
{
    qDebug() << "NodeEditorWindow::setupSceneInternal() called";
    qDebug() << "Current thread:" << QThread::currentThread();
    qDebug() << "This object thread:" << this->thread();

    if (QThread::currentThread() != this->thread()) {
        qWarning() << "setupSceneInternal() called from different thread!";
        return;
    }

    if (m_scene) {
        qDebug() << "Scene already created, skipping";
        return;
    }

    // Create graph model
    try {
        m_graphModel = new QtNodes::DataFlowGraphModel(m_registry);
        qDebug() << "Graph model created, address:" << (void*)m_graphModel;
    } catch (const std::exception& e) {
        qCritical() << "Exception during graph model creation:" << e.what();
        return;
    }

    // Try creating scene
    try {
        qDebug() << "About to create DataFlowGraphicsScene...";
        m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, nullptr);
        qDebug() << "Scene created successfully, address:" << (void*)m_scene;

        m_scene->setParent(this);
        qDebug() << "Scene parent set";
    } catch (const std::exception& e) {
        qCritical() << "Exception during scene creation:" << e.what();
        return;
    } catch (...) {
        qCritical() << "Unknown exception during scene creation";
        return;
    }

    // Set scene size
    m_scene->setSceneRect(-5000, -5000, 10000, 10000);
    qDebug() << "Scene rect set";

    // Create view
    m_view = new QtNodes::GraphicsView(m_scene);
    qDebug() << "View created";

    // Add view to layout
    m_layout->addWidget(m_view);
    qDebug() << "View added to layout";
}

void NodeEditorWindow::setupUi()
{
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    m_layout = new QVBoxLayout(centralWidget);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

void NodeEditorWindow::setupToolbar()
{
    m_toolbar = addToolBar("Main Toolbar");

    m_actionNew = new QAction("New", this);
    m_actionNew->setShortcut(QKeySequence::New);
    m_actionNew->setStatusTip("Create new graph");
    connect(m_actionNew, &QAction::triggered, this, &NodeEditorWindow::onNew);

    m_actionSave = new QAction("Save", this);
    m_actionSave->setShortcut(QKeySequence::Save);
    m_actionSave->setStatusTip("Save graph to file");
    connect(m_actionSave, &QAction::triggered, this, &NodeEditorWindow::onSave);

    m_actionLoad = new QAction("Load", this);
    m_actionLoad->setShortcut(QKeySequence::Open);
    m_actionLoad->setStatusTip("Load graph from file");
    connect(m_actionLoad, &QAction::triggered, this, &NodeEditorWindow::onLoad);

    m_actionClear = new QAction("Clear", this);
    m_actionClear->setStatusTip("Clear all nodes");
    connect(m_actionClear, &QAction::triggered, this, &NodeEditorWindow::onClear);

    m_actionDelete = new QAction("Delete", this);
    m_actionDelete->setShortcut(QKeySequence::Delete);
    m_actionDelete->setStatusTip("Delete selected nodes and connections");
    connect(m_actionDelete, &QAction::triggered, this, &NodeEditorWindow::onDelete);

    m_actionExit = new QAction("Exit", this);
    m_actionExit->setShortcut(QKeySequence::Quit);
    m_actionExit->setStatusTip("Exit node editor");
    connect(m_actionExit, &QAction::triggered, this, &NodeEditorWindow::close);

    m_toolbar->addAction(m_actionNew);
    m_toolbar->addAction(m_actionSave);
    m_toolbar->addAction(m_actionLoad);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actionClear);
    m_toolbar->addAction(m_actionDelete);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actionExit);

    statusBar()->showMessage("Ready");
}

void NodeEditorWindow::setupMenu()
{
    auto *menuFile = menuBar()->addMenu("File");
    menuFile->addAction(m_actionNew);
    menuFile->addAction(m_actionSave);
    menuFile->addAction(m_actionLoad);
    menuFile->addSeparator();
    menuFile->addAction(m_actionExit);

    auto *menuEdit = menuBar()->addMenu("Edit");
    menuEdit->addAction(m_actionDelete);
    menuEdit->addSeparator();
    menuEdit->addAction(m_actionClear);

    auto *menuHelp = menuBar()->addMenu("Help");
    auto *actionAbout = new QAction("About", this);
    connect(actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About",
                         "InSAR Node Editor\n\n"
                         "Visual node-based workflow editor\n"
                         "For InSAR data processing workflow design");
    });
    menuHelp->addAction(actionAbout);
}

void NodeEditorWindow::applyStyles()
{
    // Set dark theme styles
    QtNodes::ConnectionStyle::setConnectionStyle(
        "{"
        "\"ConnectionStyle\": {"
        "\"ConstructionColor\": \"gray\","
        "\"NormalColor\": \"#00AA00\","
        "\"SelectedColor\": \"#FF0000\","
        "\"SelectedHaloColor\": \"#FFAA00\","
        "\"HoveredColor\": \"#55FF55\","
        "\"LineWidth\": 3.0,"
        "\"UseDataDefinedColors\": false"
        "}"
        "}");

    QtNodes::NodeStyle::setNodeStyle(
        "{"
        "\"NodeStyle\": {"
        "\"NormalBoundaryColor\": \"#2a5a8f\","
        "\"SelectedBoundaryColor\": \"#00FF00\","
        "\"GradientColor0\": \"#1a3a5f\","
        "\"GradientColor1\": \"#2a5a8f\","
        "\"GradientColor2\": \"#3a7aaf\","
        "\"GradientColor3\": \"#4a9acf\","
        "\"ShadowColor\": \"#000000\","
        "\"FontColor\": \"white\","
        "\"FontFamily\": \"Arial\","
        "\"FontSize\": 12"
        "}"
        "}");

    QtNodes::GraphicsViewStyle::setStyle(
        "{"
        "\"GraphicsViewStyle\": {"
        "\"BackgroundColor\": \"#2b2b2b\","
        "\"GridColor\": \"#404040\","
        "\"FineGridColor\": \"#303030\","
        "\"ConnectionPointColor\": \"#AAAAAA\","
        "\"HoveredConnectionPointColor\": \"#FF0000\","
        "\"FlowViewColor\": \"#2b2b2b\","
        "\"FontSize\": 10"
        "}"
        "}");
}

QString NodeEditorWindow::getSaveFilePath()
{
    return QFileDialog::getSaveFileName(
        this,
        "Save Graph",
        m_currentFilePath.isEmpty() ? QDir::currentPath() : m_currentFilePath,
        "JSON Files (*.json);;All Files (*)"
    );
}

QString NodeEditorWindow::getOpenFilePath()
{
    return QFileDialog::getOpenFileName(
        this,
        "Open Graph",
        QDir::currentPath(),
        "JSON Files (*.json);;All Files (*)"
    );
}

void NodeEditorWindow::onNew()
{
    // Clear scene
    onClear();
    m_currentFilePath.clear();
    statusBar()->showMessage("New graph created");
}

void NodeEditorWindow::onSave()
{
    QString filePath = getSaveFilePath();
    if (filePath.isEmpty())
        return;

    if (m_scene->save())
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
        statusBar()->showMessage("Saved: " + filePath);
    }
    else
    {
        QMessageBox::warning(this, "Error", "Cannot save file: " + filePath);
    }
}

void NodeEditorWindow::onLoad()
{
    QString filePath = getOpenFilePath();
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, "Error", "Cannot open file: " + filePath);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isObject())
    {
        QMessageBox::warning(this, "Error", "Invalid file format");
        return;
    }

    onClear();
    if (m_scene->load())
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
        statusBar()->showMessage("Loaded: " + filePath);
    }
    else
    {
        QMessageBox::warning(this, "Error", "Cannot load file: " + filePath);
    }
}

void NodeEditorWindow::onClear()
{
    if (!m_graphModel)
        return;

    // Delete all nodes
    auto nodeIds = m_graphModel->allNodeIds();
    for (auto nodeId : nodeIds)
    {
        m_graphModel->deleteNode(nodeId);
    }

    // All connections are automatically removed when nodes are deleted
    statusBar()->showMessage("Cleared all nodes");
}

void NodeEditorWindow::onDelete()
{
    // Delete selected nodes and connections
    // QtNodes scene handles deletion operation via Delete key
    statusBar()->showMessage("Use Delete key to delete selected items");
}

void NodeEditorWindow::onSceneModified(QtNodes::BasicGraphicsScene *)
{
    if (!m_graphModel)
        return;

    int nodeCount = m_graphModel->allNodeIds().size();

    // Count all connections
    int connectionCount = 0;
    auto nodeIds = m_graphModel->allNodeIds();
    for (auto nodeId : nodeIds)
    {
        auto connections = m_graphModel->allConnectionIds(nodeId);
        connectionCount += connections.size();
    }

    statusBar()->showMessage(QString("Nodes: %1, Connections: %2").arg(nodeCount).arg(connectionCount / 2));
}

void NodeEditorWindow::onSceneLoaded()
{
    if (m_view)
        m_view->centerScene();
}
