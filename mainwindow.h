#pragma once

#include <QMainWindow>

class QPlainTextEdit;

class MainWindow : public QMainWindow {
    public:
        explicit MainWindow(QWidget *parent = nullptr);
    
    private:
        QPlainTextEdit *m_editor;
};
