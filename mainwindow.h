#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QStringList>

class QTabWidget;
class QLabel;
class QMenu;
class QTimer;
class LogWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void openStdin();

private slots:
    void onOpenFile();
    void onRunCommand();
    void onCloseTab(int index);
    void onAbout();
    void updateMemoryLabel();   // updates DB size indicator every 2 s

private:
    void setupUi();
    void openFile(const QString& filePath);
    void openCommand(const QString& command);
    void loadSettings();
    void saveSettings() const;
    void addRecentFile(const QString& filePath);
    void rebuildRecentFilesMenu();
    void clearRecentFiles();

    QTabWidget* m_tabWidget    = nullptr;
    QLabel*     m_labelMemory  = nullptr;  // status bar: SQLite DB size
    QMenu*      m_recentFilesMenu = nullptr;
    QTimer*     m_memTimer     = nullptr;
    QStringList m_recentFiles;
    int m_nextFileId = 1;
};

#endif // MAINWINDOW_H
