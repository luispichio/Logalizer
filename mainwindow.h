#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>

class QTabWidget;
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

private:
    void setupUi();
    void openFile(const QString& filePath);

    QTabWidget* m_tabWidget = nullptr;
    int m_nextFileId = 1;
};

#endif // MAINWINDOW_H
