#include "mainwindow.h"
#include "logwidget.h"

#include <QTabWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QAction>
#include <QVBoxLayout>
#include <QtLogging>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    setWindowTitle("Logalizer");
    resize(1200, 800);

    // ─── Central widget with tab ─────────────────────────────────────
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onCloseTab);

    layout->addWidget(m_tabWidget);
    setCentralWidget(central);

    // ─── Menu bar ────────────────────────────────────────────────────
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* openAction = new QAction(QIcon::fromTheme("document-open"), "&Open...", this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
    fileMenu->addAction(openAction);

    fileMenu->addSeparator();

    auto* exitAction = new QAction("E&xit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = new QAction(QIcon::fromTheme("help-about"), "&About", this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addAction(aboutAction);

    // ─── Toolbar ─────────────────────────────────────────────────────
    auto* toolBar = addToolBar("Main");
    toolBar->addAction(openAction);
    toolBar->addSeparator();
    toolBar->addAction(aboutAction);

    // ─── Status bar ──────────────────────────────────────────────────
    statusBar()->showMessage("Ready");
}

void MainWindow::onOpenFile() {
    QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "Open Log File(s)",
        QString(),
        "Log files (*.log *.jsonl *.ndjson *.txt);;All files (*)");

    for (const QString& path : filePaths) {
        openFile(path);
    }
}

void MainWindow::openFile(const QString& filePath) {
    int fileId = m_nextFileId++;

    auto* widget = new LogWidget(filePath, fileId, this);

    QFileInfo fi(filePath);
    int tabIndex = m_tabWidget->addTab(widget, fi.fileName());
    m_tabWidget->setCurrentIndex(tabIndex);
    m_tabWidget->setTabToolTip(tabIndex, filePath);

    statusBar()->showMessage(QString("Opened: %1").arg(filePath), 5000);
    qInfo() << "MainWindow: Opened file" << filePath << "as file_id" << fileId;
}

void MainWindow::onCloseTab(int index) {
    auto* widget = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);

    // LogWidget destructor handles DROP TABLE and thread cleanup
    delete widget;

    statusBar()->showMessage("Tab closed", 3000);
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About Logalizer",
                       "<h3>Logalizer</h3>"
                       "<p>High-performance JSON Lines log analyzer.</p>"
                       "<p>Built with Qt6 + SQLite FTS5.</p>"
                       "<p>Version 0.1</p>");
}
