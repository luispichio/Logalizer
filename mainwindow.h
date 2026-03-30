#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>

class QTabWidget;
class QLabel;
class QTimer;
class LogWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onOpenFile();
    void onCloseTab(int index);
    void onAbout();
    void updateMemoryLabel();   // updates DB size indicator every 2 s

private:
    void setupUi();
    void openFile(const QString& filePath);

    QTabWidget* m_tabWidget    = nullptr;
    QLabel*     m_labelMemory  = nullptr;  // status bar: SQLite DB size
    QTimer*     m_memTimer     = nullptr;
    int m_nextFileId = 1;
};

#endif // MAINWINDOW_H
