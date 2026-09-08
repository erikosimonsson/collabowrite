#pragma once

#include <QMainWindow>
#include <QString>

class QPlainTextEdit;

class MainWindow : public QMainWindow {
    public:
        explicit MainWindow(QWidget *parent = nullptr);
    
    private:
        void newPage();
        void openFile();
        void saveFile();
        bool writeFile(const QString &filePath);
        void updateWindowTitle();

        QPlainTextEdit *m_editor;
        QString m_currentFilePath;
};
