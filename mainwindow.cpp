#include "mainwindow.h"
#include "logwidget.h"
#include "logdatabase.h"
#include "version.h"

#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QTabWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QAction>
#include <QSettings>
#include <QVBoxLayout>
#include <QtCore/QtLogging>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
}

MainWindow::~MainWindow() {
    saveSettings();
}

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

    m_recentFilesMenu = fileMenu->addMenu("Recent Files");

    auto* runCommandAction = new QAction(QIcon::fromTheme("system-run"), "&Run Command...", this);
    runCommandAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    connect(runCommandAction, &QAction::triggered, this, &MainWindow::onRunCommand);
    fileMenu->addAction(runCommandAction);

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
    toolBar->addAction(runCommandAction);
    toolBar->addSeparator();
    toolBar->addAction(aboutAction);

    // ─── Status bar ──────────────────────────────────────────────────
    m_labelMemory = new QLabel("DB: 0 MB", this);
    m_labelMemory->setToolTip("SQLite in-memory database used/reserved pages");
    statusBar()->addPermanentWidget(m_labelMemory);
    statusBar()->showMessage("Ready");

    // Update memory label every 2 seconds
    m_memTimer = new QTimer(this);
    m_memTimer->setInterval(2000);
    connect(m_memTimer, &QTimer::timeout, this, &MainWindow::updateMemoryLabel);
    m_memTimer->start();

    loadSettings();
}

void MainWindow::updateMemoryLabel() {
    const qint64 usedBytes = LogDatabase::instance().totalDbUsedBytes();
    const qint64 reservedBytes = LogDatabase::instance().totalDbSizeBytes();
    auto formatBytes = [](qint64 bytes) {
        if (bytes < 1024 * 1024) {
            return QString("%1 KB").arg(bytes / 1024);
        }
        return QString("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    };
    m_labelMemory->setText(QString("DB: %1 used / %2 reserved").arg(formatBytes(usedBytes), formatBytes(reservedBytes)));
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

void MainWindow::onRunCommand() {
    bool ok = false;
    const QString command = QInputDialog::getText(
        this,
        "Run Command",
        "Command:",
        QLineEdit::Normal,
        QString(),
        &ok).trimmed();

    if (!ok || command.isEmpty()) {
        return;
    }

    openCommand(command);
}

void MainWindow::openFile(const QString& filePath) {
    int fileId = m_nextFileId++;

    auto* widget = new LogWidget(filePath, fileId, this);

    addRecentFile(filePath);

    QFileInfo fi(filePath);
    int tabIndex = m_tabWidget->addTab(widget, fi.fileName());
    m_tabWidget->setCurrentIndex(tabIndex);
    m_tabWidget->setTabToolTip(tabIndex, filePath);

    statusBar()->showMessage(QString("Opened: %1").arg(filePath), 5000);
    qInfo() << "MainWindow: Opened file" << filePath << "as file_id" << fileId;
}

void MainWindow::loadSettings() {
    QSettings settings("Logalizer", "Logalizer");
    m_recentFiles = settings.value("mainWindow/recentFiles").toStringList();
    m_recentFiles.removeAll(QString());
    while (m_recentFiles.size() > 15) {
        m_recentFiles.removeLast();
    }
    rebuildRecentFilesMenu();
}

void MainWindow::saveSettings() const {
    QSettings settings("Logalizer", "Logalizer");
    settings.setValue("mainWindow/recentFiles", m_recentFiles);
}

void MainWindow::addRecentFile(const QString& filePath) {
    const QString cleanPath = QFileInfo(filePath).absoluteFilePath();
    if (cleanPath.isEmpty()) {
        return;
    }

    m_recentFiles.removeAll(cleanPath);
    m_recentFiles.prepend(cleanPath);
    while (m_recentFiles.size() > 15) {
        m_recentFiles.removeLast();
    }
    rebuildRecentFilesMenu();
    saveSettings();
}

void MainWindow::rebuildRecentFilesMenu() {
    if (!m_recentFilesMenu) {
        return;
    }

    m_recentFilesMenu->clear();
    for (const QString& path : m_recentFiles) {
        const QFileInfo fi(path);
        auto* action = m_recentFilesMenu->addAction(fi.fileName().isEmpty() ? path : fi.fileName());
        action->setToolTip(path);
        action->setData(path);
        connect(action, &QAction::triggered, this, [this, path]() {
            if (!QFileInfo::exists(path)) {
                statusBar()->showMessage(QString("Recent file not found: %1").arg(path), 5000);
                m_recentFiles.removeAll(path);
                rebuildRecentFilesMenu();
                saveSettings();
                return;
            }
            openFile(path);
        });
    }

    if (!m_recentFiles.isEmpty()) {
        m_recentFilesMenu->addSeparator();
    }

    auto* clearAction = m_recentFilesMenu->addAction("Clear Recent Files");
    clearAction->setEnabled(!m_recentFiles.isEmpty());
    connect(clearAction, &QAction::triggered, this, [this]() { clearRecentFiles(); });
}

void MainWindow::clearRecentFiles() {
    m_recentFiles.clear();
    rebuildRecentFilesMenu();
    saveSettings();
    statusBar()->showMessage("Recent files cleared", 3000);
}

void MainWindow::openStdin() {
    int fileId = m_nextFileId++;

    auto* widget = new LogWidget(LogWidget::SourceType::Stdin, "stdin", fileId, this);

    int tabIndex = m_tabWidget->addTab(widget, "stdin");
    m_tabWidget->setCurrentIndex(tabIndex);
    m_tabWidget->setTabToolTip(tabIndex, "Standard input stream");

    statusBar()->showMessage("Reading from stdin", 5000);
    qInfo() << "MainWindow: Opened stdin as file_id" << fileId;
}

void MainWindow::openCommand(const QString& command) {
    int fileId = m_nextFileId++;

    auto* widget = new LogWidget(LogWidget::SourceType::Command, command, fileId, this);

    QString tabTitle = command.simplified();
    if (tabTitle.size() > 32) {
        tabTitle = tabTitle.left(29) + "...";
    }

    int tabIndex = m_tabWidget->addTab(widget, tabTitle);
    m_tabWidget->setCurrentIndex(tabIndex);
    m_tabWidget->setTabToolTip(tabIndex, command);

    statusBar()->showMessage(QString("Running: %1").arg(command), 5000);
    qInfo() << "MainWindow: Running command" << command << "as file_id" << fileId;
}

void MainWindow::onCloseTab(int index) {
    auto* widget = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);

    // LogWidget destructor handles DROP TABLE and thread cleanup
    delete widget;

    statusBar()->showMessage("Tab closed", 3000);
}

void MainWindow::onAbout() {
    QMessageBox* box = new QMessageBox(this);
    box->setWindowTitle("About Logalizer");
    box->setTextFormat(Qt::RichText);
    box->setTextInteractionFlags(Qt::TextBrowserInteraction);
    box->setText(
        "<h2>Logalizer</h2>"
        "<p>High-performance log file analyzer.<br>"
        "A simple, zero-configuration desktop alternative to <code>lnav</code>.</p>"
        "<ul>"
        "<li>FTS5-backed text search and row navigation</li>"
        "<li>Multi-threaded background ingestion</li>"
        "<li>Single in-memory FTS table per file</li>"
        "<li>Multi-file support with per-tab isolation</li>"
        "</ul>"
        "<p><b>Stack:</b> C++17 &bull; Qt6 &bull; SQLite FTS5 (in-memory)</p>"
        "<p><b>Version:</b> " APP_VERSION_FULL "</p>"
        "<p>&#128279; "
        "<a href='https://github.com/luispichio/Logalizer'>"
        "github.com/luispichio/Logalizer</a></p>"
        "<p><b>Author:</b> "
        "<a href='https://luispichio.github.io/'>Luis Pichio | https://luispichio.github.io/</a></p>"
    );
    box->setStandardButtons(QMessageBox::Ok);
    box->exec();
    delete box;
}
